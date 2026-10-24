#include "raiders/Log.h"

#include <chrono>

#if RA_ENABLE_LOGGING
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace raiders {
namespace {
constexpr std::uint64_t jsonIntegerMax = 9007199254740991ULL;
std::atomic<std::uint64_t> nextEventId{1};
bool ValidId(std::string_view id) {
    auto alnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (id.empty() || id.size() > 128 || !alnum(id.front())) return false;
    for (unsigned char c : id) {
        if (!alnum(c) && c != '.' && c != '_' && c != ':' && c != '/' && c != '-') return false;
    }
    return true;
}
bool ValidLevel(LogLevel level) { return level >= LogLevel::Trace && level <= LogLevel::Off; }
}

struct Log::Impl {
    explicit Impl(LogConfig value) : config(std::move(value)) {
        if (!ValidId(config.session_id) || !ValidId(config.process_instance_id) ||
            !ValidId(config.clock_domain_id) || config.path.empty() || !config.queue_capacity ||
            !config.max_record_bytes || config.max_file_bytes < 2 ||
            config.max_record_bytes > config.max_file_bytes - 2 ||
            config.retained_files > 200000 || !ValidLevel(config.minimum_level)) {
            throw std::invalid_argument("Invalid logging configuration");
        }
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.path.native(), config.max_file_bytes, config.retained_files);
        backend = std::make_unique<spdlog::logger>("raiders", std::move(sink));
        backend->set_pattern("%v");
        backend->set_level(spdlog::level::trace);
        backend->set_error_handler([this](const std::string&) { ++write_errors; });
        worker = std::thread([this] { Run(); });
    }

    void Run() noexcept {
        for (;;) {
            std::string record;
            {
                std::unique_lock lock(queue_mutex);
                ready.wait(lock, [this] { return stopping || !queue.empty(); });
                if (queue.empty() && stopping) break;
                record = std::move(queue.front());
                queue.pop_front();
            }
            try {
                std::lock_guard lock(sink_mutex);
                backend->log(spdlog::level::info, "{}", record);
            } catch (...) { ++write_errors; }
            {
                std::lock_guard lock(queue_mutex);
                ++processed;
            }
            drained.notify_all();
        }
        try { backend->flush(); } catch (...) { ++write_errors; }
    }

    LogConfig config;
    std::unique_ptr<spdlog::logger> backend;
    std::mutex queue_mutex, sink_mutex, lifecycle_mutex;
    std::condition_variable ready, drained;
    std::deque<std::string> queue;
    bool stopping = false;
    std::thread worker;
    std::uint64_t accepted = 0, processed = 0;
    std::atomic<std::uint64_t> rejected{0}, dropped_full{0}, write_errors{0};
};

Log::Log(LogConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
Log::~Log() { Shutdown(); }

bool Log::Write(LogLevel level, std::string_view category, const LogContext& context,
                std::string_view message, const LogAttributes& attributes) noexcept {
    auto& state = *impl_;
    try {
        if (!ValidLevel(level)) throw std::invalid_argument("Invalid log level");
        if (level == LogLevel::Off || level < state.config.minimum_level) return false;
        const auto timestamp = ClockNowUs(); // Capture at producer, before serialization/queue wait.
        std::lock_guard lock(state.queue_mutex);
        if (state.stopping) { ++state.rejected; return false; }
        if (state.queue.size() >= state.config.queue_capacity) { ++state.dropped_full; return false; }

        std::size_t remaining = state.config.max_record_bytes;
        auto budget = [&remaining](std::size_t bytes) {
            if (bytes > remaining) throw std::length_error("Log input exceeds record budget");
            remaining -= bytes;
        };
        if (category.empty() || category.size() > 128 || attributes.size() > 64 ||
            timestamp > jsonIntegerMax || state.accepted == jsonIntegerMax) {
            throw std::invalid_argument("Invalid log record");
        }
        budget(category.size()); budget(message.size());
        nlohmann::json jsonContext = {
            {"session_id", state.config.session_id},
            {"process_instance_id", state.config.process_instance_id},
            {"clock_domain_id", state.config.clock_domain_id}
        };
        auto addId = [&](const char* key, const std::optional<std::string>& id) {
            if (id && !ValidId(*id)) throw std::invalid_argument("Invalid context ID");
            jsonContext[key] = id ? nlohmann::json(*id) : nlohmann::json(nullptr);
        };
        addId("thread_id", context.thread_id); addId("correlation_id", context.correlation_id);
        addId("asset_id", context.asset_id); addId("entity_id", context.entity_id);
        addId("widget_id", context.widget_id); addId("vm_instance_id", context.vm_instance_id);
        auto fields = nlohmann::json::object();
        for (const auto& [key, value] : attributes) {
            if (key.empty() || key.size() > 128 || fields.contains(key)) {
                throw std::invalid_argument("Invalid or duplicate attribute key");
            }
            budget(key.size());
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::string>) budget(item.size());
                if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(item)) throw std::invalid_argument("Nonfinite log attribute");
                }
                if constexpr (std::is_same_v<T, std::int64_t>) {
                    if (item < -static_cast<std::int64_t>(jsonIntegerMax) || item > static_cast<std::int64_t>(jsonIntegerMax))
                        throw std::invalid_argument("Log integer exceeds exact JSON range");
                }
                fields[key] = item;
            }, value);
        }
        const auto eventId = nextEventId.fetch_add(1);
        if (eventId > jsonIntegerMax) throw std::overflow_error("Log event ID range exhausted");
        constexpr const char* levels[] = {"trace", "debug", "info", "warning", "error", "critical"};
        nlohmann::json event = {
            {"schema_version", 1}, {"event_id", "log." + std::to_string(eventId)},
            {"timestamp_us", timestamp}, {"context", std::move(jsonContext)},
            {"severity", levels[static_cast<int>(level)]}, {"category", category},
            {"message", message}, {"attributes", std::move(fields)}
        };
        auto record = event.dump(); // Strict UTF-8 validation and JSON escaping.
        if (record.size() > state.config.max_record_bytes) throw std::length_error("Serialized log exceeds record budget");
        state.queue.push_back(std::move(record));
        ++state.accepted;
        state.ready.notify_one();
        return true;
    } catch (...) { ++state.rejected; return false; }
}

bool Log::Flush() {
    auto& state = *impl_;
    std::lock_guard lifecycle(state.lifecycle_mutex);
    {
        std::unique_lock lock(state.queue_mutex);
        const auto target = state.accepted;
        state.drained.wait(lock, [&] { return state.processed >= target; });
    }
    if (state.backend) {
        std::lock_guard sink(state.sink_mutex);
        try { state.backend->flush(); } catch (...) { ++state.write_errors; }
    }
    return state.write_errors == 0;
}

void Log::Shutdown() {
    auto& state = *impl_;
    std::lock_guard lifecycle(state.lifecycle_mutex);
    {
        std::lock_guard lock(state.queue_mutex);
        state.stopping = true;
    }
    state.ready.notify_all();
    if (state.worker.joinable()) state.worker.join();
    state.backend.reset();
}

LogStats Log::Stats() const {
    auto& state = *impl_;
    std::lock_guard lock(state.queue_mutex);
    return {state.accepted, state.processed, state.dropped_full, state.rejected, state.write_errors};
}
}
#else
namespace raiders {
struct Log::Impl {};
Log::Log(LogConfig) : impl_(std::make_unique<Impl>()) {}
Log::~Log() = default;
bool Log::Write(LogLevel, std::string_view, const LogContext&, std::string_view, const LogAttributes&) noexcept { return false; }
bool Log::Flush() { return true; }
void Log::Shutdown() {}
LogStats Log::Stats() const { return {}; }
}
#endif

std::uint64_t raiders::Log::ClockNowUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
