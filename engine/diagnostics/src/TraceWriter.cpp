#include "raiders/TraceWriter.h"

#if RA_ENABLE_PROFILING
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace raiders {
namespace {
using Json = nlohmann::json;
constexpr std::uint64_t maxInteger = 9007199254740991ULL;
constexpr std::size_t maxEventBytes = 4096;
bool Id(std::string_view value) {
    auto alnum = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (value.empty() || value.size() > 128 || !alnum(value.front())) return false;
    for (unsigned char c : value)
        if (!alnum(c) && c != '.' && c != '_' && c != ':' && c != '/' && c != '-') return false;
    return true;
}
bool Text(std::string_view value) {
    return !value.empty() && value.size() <= 128 && value.find('\0') == std::string_view::npos &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
}
bool Series(std::string_view name) {
    return Text(name) && (name.ends_with(".count") || name.ends_with(".bytes") ||
        name.ends_with(".us") || name.ends_with(".ratio"));
}
std::string Field(const ProfileText& field) {
    auto end = std::find(field.begin(), field.end(), '\0');
    if (end == field.end()) throw std::invalid_argument("Unterminated profile field");
    return {field.begin(), end};
}
Json Context(ProfileContext context) {
    Json args = Json::object();
    auto add = [&](const char* key, std::string_view value) {
        if (!value.empty()) {
            if (!Id(value)) throw std::invalid_argument("Invalid trace context ID");
            args[key] = value;
        }
    };
    add("correlation_id", context.correlation_id); add("asset_id", context.asset_id);
    add("entity_id", context.entity_id); add("widget_id", context.widget_id);
    add("vm_instance_id", context.vm_instance_id);
    return args;
}
std::uint32_t Checksum(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
void RegularPath(const std::filesystem::path& path) {
    auto flags = GetFileAttributesW(path.c_str());
    if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Trace files cannot be reparse points");
}
void AtomicWrite(const std::filesystem::path& path, std::string_view bytes, bool replace = false) {
    RegularPath(path);
    if (!replace && std::filesystem::exists(path)) throw std::runtime_error("Trace output already exists");
    auto temporary = path; temporary += L".tmp";
    RegularPath(temporary);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush(); stream.close();
    }
    const auto flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags)) throw std::runtime_error("Trace checkpoint rename failed");
}
std::string Read(const std::filesystem::path& path, std::size_t limit) {
    RegularPath(path);
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("Trace recovery size limit exceeded");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}
struct CaptureLock {
    explicit CaptureLock(const std::filesystem::path& directory, bool create) {
        auto path = directory / "capture.lock";
        RegularPath(path);
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            create ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Trace capture is active or inaccessible");
    }
    ~CaptureLock() { CloseHandle(handle); }
    HANDLE handle;
};
std::string ChunkName(std::size_t index) { return "chunk-" + std::to_string(index) + ".json"; }
Json Coverage(std::uint64_t drops, bool partial, std::string_view reason, std::size_t capacity) {
    return {{"status", partial ? "partial" : "complete"},
        {"scope", "Shared writer pipeline; loss counts across signals are conservative and not additive"},
        {"reason", partial ? Json(reason) : Json(nullptr)}, {"dropped_records", drops},
        {"capacity_records", capacity}, {"sampling_interval_us", nullptr}};
}
// Rebuild solely from checksummed, indexed chunks; never discover arbitrary paths.
Json Assemble(const std::filesystem::path& directory, bool recovered) {
    auto wrapper = Json::parse(Read(directory / "recovery-index.json", 1024 * 1024));
    auto index = wrapper.at("payload");
    if (wrapper.at("crc32").get<std::uint32_t>() != Checksum(index.dump()) || index.at("version") != 1)
        throw std::runtime_error("Invalid trace recovery index");
    const auto chunkSize = index.at("events_per_chunk").get<std::size_t>();
    const auto maxChunks = index.at("max_chunks").get<std::size_t>();
    if (!chunkSize || chunkSize > 1024 || !maxChunks || maxChunks > 256 || index.at("chunks").size() > maxChunks)
        throw std::runtime_error("Invalid recovery limits");
    std::vector<std::pair<std::size_t, Json>> records;
    std::map<std::string, std::size_t> open;
    std::set<std::size_t> omit;
    for (std::size_t block = 0; block < index.at("chunks").size(); ++block) {
        const auto& entry = index.at("chunks")[block];
        if (entry.at("name") != ChunkName(block)) throw std::runtime_error("Invalid chunk path");
        auto bytes = Read(directory / ChunkName(block), chunkSize * (maxEventBytes + 1) + 2);
        if (entry.at("crc32").get<std::uint32_t>() != Checksum(bytes)) throw std::runtime_error("Corrupt trace chunk");
        auto events = Json::parse(bytes);
        if (!events.is_array() || events.size() > chunkSize) throw std::runtime_error("Invalid trace chunk");
        for (auto& event : events) {
            const auto phase = event.at("ph").get<std::string>();
            if (phase == "b") {
                auto id = event.at("id").get<std::string>();
                if (open.contains(id)) throw std::runtime_error("Duplicate async begin");
                open[id] = records.size();
            } else if (phase == "e") {
                auto id = event.at("id").get<std::string>();
                auto found = open.find(id);
                if (found == open.end()) throw std::runtime_error("Async end without begin");
                const auto& begin = records[found->second].second;
                if (begin.at("name") != event.at("name") || begin.at("cat") != event.at("cat") || begin.at("ts") > event.at("ts"))
                    throw std::runtime_error("Inconsistent async pair");
                open.erase(found);
            } else if (phase != "X" && phase != "C" && phase != "M") throw std::runtime_error("Unsupported trace phase");
            records.emplace_back(records.size(), std::move(event));
        }
    }
    for (const auto& [id, position] : open) { (void)id; omit.insert(position); }
    records.erase(std::remove_if(records.begin(), records.end(), [&](const auto& item) { return omit.contains(item.first); }), records.end());
    auto rank = [](const Json& event) {
        const auto phase = event.at("ph").get<std::string>();
        return phase == "M" ? 0 : phase == "X" ? 1 : 2;
    };
    std::stable_sort(records.begin(), records.end(), [&](const auto& a, const auto& b) {
        if (a.second.at("ts") != b.second.at("ts")) return a.second.at("ts") < b.second.at("ts");
        if (rank(a.second) != rank(b.second)) return rank(a.second) < rank(b.second);
        if (a.second.at("ph") == "X") {
            if (a.second.at("dur") != b.second.at("dur")) return a.second.at("dur") > b.second.at("dur");
            return a.first > b.first; // Equal-resolution nested scopes complete inner-first.
        }
        return a.first < b.first; // Preserve async source order at equal timestamps.
    });
    Json manifest = index.at("manifest");
    manifest["traceEvents"] = Json::array();
    std::map<std::uint64_t, std::vector<std::uint64_t>> nesting;
    std::size_t overlaps = 0;
    for (auto& [position, event] : records) {
        (void)position;
        if (event.at("ph") == "X") {
            auto& stack = nesting[event.at("tid").get<std::uint64_t>()];
            const auto start = event.at("ts").get<std::uint64_t>();
            const auto end = start + event.at("dur").get<std::uint64_t>();
            while (!stack.empty() && start >= stack.back()) stack.pop_back();
            if (!stack.empty() && end > stack.back()) { ++overlaps; continue; }
            stack.push_back(end);
        }
        manifest["traceEvents"].push_back(std::move(event));
    }
    if (recovered || !omit.empty() || overlaps) {
        for (auto& collector : manifest["collectors"]) {
            auto& coverage = collector["coverage"];
            coverage["status"] = "partial";
            coverage["reason"] = "Interrupted recovery, unfinished async operations or invalid scope overlaps; only indexed complete blocks retained";
            coverage["dropped_records"] = std::min(maxInteger,
                coverage["dropped_records"].get<std::uint64_t>() + omit.size() + overlaps);
        }
    }
    return manifest;
}
}

struct TraceWriter::Impl {
    explicit Impl(TraceConfig value) : config(std::move(value)) {
        if (!Id(config.session_id) || !Id(config.process_instance_id) || !Id(config.clock_domain_id) ||
            !Id(config.build_id) || !Text(config.process_name) || config.trace_pid > maxInteger ||
            !config.events_per_chunk || config.events_per_chunk > 1024 || !config.max_chunks || config.max_chunks > 256 ||
            !config.max_threads || config.max_threads > 1024 || !config.max_async || config.max_async > 1024 ||
            config.content_mode < TraceContentMode::SerializedEditing || config.content_mode > TraceContentMode::Shipping || config.directory.empty())
            throw std::invalid_argument("Invalid trace configuration");
        if (!std::filesystem::create_directory(config.directory)) throw std::runtime_error("Trace capture directory must be new");
        lock = std::make_unique<CaptureLock>(config.directory, true);
        pending = Json::array(); chunks = Json::array();
        pending.push_back(Event(0, "metadata", "process_name", "M", 0, {{"name", config.process_name}}));
        ++stats.accepted;
        if (!Checkpoint()) throw std::runtime_error("Cannot initialize trace checkpoint");
    }
    Json Event(std::uint64_t tid, std::string_view category, std::string_view name,
               std::string_view phase, std::uint64_t timestamp, Json args) const {
        if (tid > maxInteger || timestamp > maxInteger || !Text(category) || !Text(name))
            throw std::invalid_argument("Invalid trace event");
        return {{"name", name}, {"cat", category}, {"ph", phase}, {"pid", config.trace_pid},
            {"tid", tid}, {"ts", timestamp}, {"args", std::move(args)}};
    }
    Json Manifest() const {
        const char* modes[] = {"serialized-editing", "packaging-debug", "shipping"};
        auto collectors = Json::array();
        for (auto signal : {"scopes", "counters", "flows"}) {
            bool scopes = std::string_view(signal) == "scopes";
            bool incomplete = failed || stats.dropped || stats.rejected ||
                (scopes && (!observed || profile.accepting || profile.in_flight || profile.buffered || profile.completed != scopes_received));
            const auto producerDrops = scopes ? profile.dropped_full : 0;
            auto drops = stats.dropped >= maxInteger - producerDrops ? maxInteger : stats.dropped + producerDrops;
            incomplete = incomplete || drops || (scopes && profile.rejected);
            collectors.push_back({{"id", std::string("native.") + signal}, {"process_instance_id", config.process_instance_id},
                {"signal", signal}, {"method", "native-trace-writer/v1"},
                {"coverage", Coverage(drops, incomplete, "Writer/producer loss, unobserved collector or unfinished profile drain", config.events_per_chunk * config.max_chunks)}});
        }
        return {{"schema_version", 1}, {"session_id", config.session_id}, {"build_id", config.build_id},
            {"content_mode", modes[static_cast<int>(config.content_mode)]}, {"clock_domain_id", config.clock_domain_id},
            {"clock_domains", Json::array({{{"id", config.clock_domain_id}, {"source", "std::chrono::steady_clock"}, {"monotonic", true}, {"resolution_us", nullptr}}})},
            {"processes", Json::array({{{"process_instance_id", config.process_instance_id}, {"trace_pid", config.trace_pid},
                {"os_pid", GetCurrentProcessId()}, {"name", config.process_name}, {"language", "cpp"}, {"build_id", config.build_id}}})},
            {"clock_sync", Json::array()}, {"collectors", std::move(collectors)},
            {"traceEvents", Json::array()}, {"samples", Json::array()}};
    }
    bool Checkpoint() {
        if (failed) return false;
        try {
            if (!pending.empty()) {
                const auto name = ChunkName(chunks.size());
                const auto bytes = pending.dump();
                AtomicWrite(config.directory / name, bytes);
                chunks.push_back({{"name", name}, {"crc32", Checksum(bytes)}});
                pending.clear();
            }
            Json index = {{"version", 1}, {"events_per_chunk", config.events_per_chunk}, {"max_chunks", config.max_chunks},
                {"chunks", chunks}, {"manifest", Manifest()}};
            AtomicWrite(config.directory / "recovery-index.json", Json({{"payload", index}, {"crc32", Checksum(index.dump())}}).dump(), true);
            return true;
        } catch (...) { failed = true; ++stats.io_errors; return false; }
    }
    bool Push(Json event) {
        if (stats.finalized || failed) { ++stats.rejected; return false; }
        if (chunks.size() >= config.max_chunks) { ++stats.dropped; return false; }
        if (event.dump().size() > maxEventBytes) { ++stats.rejected; return false; }
        pending.push_back(std::move(event)); ++stats.accepted;
        if (pending.size() == config.events_per_chunk) return Checkpoint();
        return true;
    }
    bool Thread(std::uint64_t tid, std::string_view name = {}) {
        if (tid > maxInteger) throw std::invalid_argument("Invalid thread identity");
        if (threads.contains(tid) && name.empty()) return true;
        if (!threads.contains(tid) && threads.size() >= config.max_threads) { ++stats.dropped; return false; }
        auto label = name.empty() ? "thread." + std::to_string(tid) : std::string(name);
        if (!Text(label)) throw std::invalid_argument("Invalid thread name");
        if (!Push(Event(tid, "metadata", "thread_name", "M", 0, {{"name", label}, {"thread_instance_id", std::to_string(tid)}}))) return false;
        threads[tid] = std::move(label);
        return true;
    }
    TraceConfig config;
    std::unique_ptr<CaptureLock> lock;
    mutable std::mutex mutex;
    TraceStats stats;
    ProfileStats profile;
    bool observed = false, failed = false;
    std::uint64_t scopes_received = 0;
    Json pending, chunks;
    std::map<std::uint64_t, std::string> threads;
    std::map<std::string, Json> async;
};

TraceWriter::TraceWriter(TraceConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
TraceWriter::~TraceWriter() { try { if (!impl_->stats.finalized) Checkpoint(); } catch (...) {} }
bool TraceWriter::RegisterThread(std::uint64_t tid, std::string_view name) {
    std::lock_guard guard(impl_->mutex);
    try { return impl_->Thread(tid, name); } catch (...) { ++impl_->stats.rejected; return false; }
}
bool TraceWriter::AppendScope(const ProfileEvent& scope) {
    std::lock_guard guard(impl_->mutex);
    ++impl_->scopes_received;
    try {
        if (scope.duration_us > maxInteger || scope.timestamp_us > maxInteger - scope.duration_us) throw std::invalid_argument("Invalid duration");
        auto correlation = Field(scope.correlation_id), asset = Field(scope.asset_id), entity = Field(scope.entity_id);
        auto widget = Field(scope.widget_id), vm = Field(scope.vm_instance_id);
        auto event = impl_->Event(scope.thread_id, Field(scope.category), Field(scope.name), "X", scope.timestamp_us,
            Context({correlation, asset, entity, widget, vm}));
        event["dur"] = scope.duration_us;
        return impl_->Thread(scope.thread_id) && impl_->Push(std::move(event));
    } catch (...) { ++impl_->stats.rejected; return false; }
}
bool TraceWriter::Counter(std::uint64_t tid, std::string_view category, std::string_view name, std::string_view series, double value) {
    const auto timestamp = ProfileCapture::ClockNowUs();
    std::lock_guard guard(impl_->mutex);
    try {
        if (!Series(series) || !std::isfinite(value)) throw std::invalid_argument("Invalid counter");
        auto event = impl_->Event(tid, category, name, "C", timestamp, {{std::string(series), value}});
        return impl_->Thread(tid) && impl_->Push(std::move(event));
    } catch (...) { ++impl_->stats.rejected; return false; }
}
bool TraceWriter::AsyncBegin(std::uint64_t tid, std::string_view category, std::string_view name, std::string_view id, ProfileContext context) {
    const auto timestamp = ProfileCapture::ClockNowUs();
    std::lock_guard guard(impl_->mutex);
    try {
        if (!Id(id) || impl_->async.contains(std::string(id))) throw std::invalid_argument("Invalid/duplicate async ID");
        if (impl_->async.size() >= impl_->config.max_async) { ++impl_->stats.dropped; return false; }
        auto event = impl_->Event(tid, category, name, "b", timestamp, Context(context)); event["id"] = id;
        if (!impl_->Thread(tid) || !impl_->Push(event)) return false;
        impl_->async.emplace(std::string(id), std::move(event));
        return true;
    } catch (...) { ++impl_->stats.rejected; return false; }
}
bool TraceWriter::AsyncEnd(std::uint64_t tid, std::string_view id) {
    const auto timestamp = ProfileCapture::ClockNowUs();
    std::lock_guard guard(impl_->mutex);
    try {
        if (!Id(id)) throw std::invalid_argument("Invalid async ID");
        auto found = impl_->async.find(std::string(id));
        if (found == impl_->async.end()) throw std::invalid_argument("Async begin missing");
        auto event = found->second;
        if (timestamp < event["ts"].get<std::uint64_t>() || tid > maxInteger) throw std::invalid_argument("Invalid async end");
        event["ph"] = "e"; event["tid"] = tid; event["ts"] = timestamp;
        if (!impl_->Thread(tid) || !impl_->Push(std::move(event))) return false;
        impl_->async.erase(found);
        return true;
    } catch (...) { ++impl_->stats.rejected; return false; }
}
bool TraceWriter::ObserveProfile(const ProfileStats& value) {
    std::lock_guard guard(impl_->mutex);
    if (impl_->stats.finalized || !value.capacity || (impl_->observed && value.capacity != impl_->profile.capacity) || value.accepted < impl_->profile.accepted ||
        value.completed < impl_->profile.completed || value.dropped_full < impl_->profile.dropped_full ||
        value.rejected < impl_->profile.rejected || value.capacity > maxInteger ||
        value.in_flight > value.capacity || value.buffered > value.capacity - value.in_flight ||
        value.accepted > maxInteger || value.completed > value.accepted || value.dropped_full > maxInteger || value.rejected > maxInteger) {
        ++impl_->stats.rejected; return false;
    }
    impl_->profile = value; impl_->observed = true;
    return true;
}
bool TraceWriter::Checkpoint() {
    std::lock_guard guard(impl_->mutex);
    return impl_->stats.finalized || impl_->Checkpoint();
}
bool TraceWriter::Finalize() {
    std::lock_guard guard(impl_->mutex);
    if (impl_->stats.finalized) return true;
    if (!impl_->Checkpoint()) return false;
    try {
        AtomicWrite(impl_->config.directory / "trace.json", Assemble(impl_->config.directory, false).dump());
        impl_->stats.finalized = true;
        return true;
    } catch (...) { impl_->failed = true; ++impl_->stats.io_errors; return false; }
}
TraceStats TraceWriter::Stats() const {
    std::lock_guard guard(impl_->mutex);
    auto result = impl_->stats;
    result.committed_chunks = impl_->chunks.size(); result.pending_events = impl_->pending.size(); result.open_async = impl_->async.size();
    return result;
}
bool TraceWriter::Recover(const std::filesystem::path& directory) {
    try {
        CaptureLock lock(directory, false);
        AtomicWrite(directory / "recovered.json", Assemble(directory, true).dump());
        return true;
    } catch (...) { return false; }
}
}
#else
namespace raiders {
struct TraceWriter::Impl {};
TraceWriter::TraceWriter(TraceConfig) : impl_(std::make_unique<Impl>()) {}
TraceWriter::~TraceWriter() = default;
bool TraceWriter::RegisterThread(std::uint64_t, std::string_view) { return false; }
bool TraceWriter::AppendScope(const ProfileEvent&) { return false; }
bool TraceWriter::Counter(std::uint64_t, std::string_view, std::string_view, std::string_view, double) { return false; }
bool TraceWriter::AsyncBegin(std::uint64_t, std::string_view, std::string_view, std::string_view, ProfileContext) { return false; }
bool TraceWriter::AsyncEnd(std::uint64_t, std::string_view) { return false; }
bool TraceWriter::ObserveProfile(const ProfileStats&) { return false; }
bool TraceWriter::Checkpoint() { return false; }
bool TraceWriter::Finalize() { return false; }
TraceStats TraceWriter::Stats() const { return {}; }
bool TraceWriter::Recover(const std::filesystem::path&) { return false; }
}
#endif
