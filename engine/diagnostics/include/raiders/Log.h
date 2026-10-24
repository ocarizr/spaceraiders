#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifndef RA_ENABLE_LOGGING
#define RA_ENABLE_LOGGING 1
#endif
#ifndef RA_LOG_ACTIVE_LEVEL
#define RA_LOG_ACTIVE_LEVEL 2
#endif

namespace raiders {
enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical, Off };
using LogValue = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string>;
using LogAttributes = std::vector<std::pair<std::string, LogValue>>;

struct LogContext {
    std::optional<std::string> thread_id;
    std::optional<std::string> correlation_id, asset_id, entity_id, widget_id, vm_instance_id;
};

struct LogConfig {
    std::filesystem::path path;
    std::string session_id, process_instance_id, clock_domain_id;
    std::size_t queue_capacity = 1024;
    std::size_t max_record_bytes = 16 * 1024;
    std::size_t max_file_bytes = 1024 * 1024;
    std::size_t retained_files = 3; // Backups in addition to the active file.
    LogLevel minimum_level = LogLevel::Info;
};

struct LogStats {
    std::uint64_t accepted = 0, processed = 0, dropped_full = 0, rejected = 0, write_errors = 0;
};

// Own one instance per output file. Stop producers before destruction.
// Configuration/open failures throw; Write never throws. Counters are the
// nonrecursive error channel. Shutdown drains accepted records and closes files.
class Log final {
public:
    explicit Log(LogConfig config);
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    bool Write(LogLevel level, std::string_view category, const LogContext& context,
               std::string_view message, const LogAttributes& attributes = {}) noexcept;
    bool Flush(); // Barrier for accepted records at entry; not a disk durability guarantee.
    void Shutdown(); // Idempotent; safe against concurrent Write/Flush calls.
    LogStats Stats() const;
    static std::uint64_t ClockNowUs(); // steady_clock time_since_epoch, truncated to us.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

// Keep formatting and all context/attribute construction inside the macro call.
// Direct Write calls still evaluate their arguments before runtime filtering.
#define RA_LOG_DETAIL(logger, level, ...) ((logger).Write(::raiders::LogLevel::level, __VA_ARGS__))
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 0
#define RA_LOG_TRACE(logger, ...) RA_LOG_DETAIL(logger, Trace, __VA_ARGS__)
#else
#define RA_LOG_TRACE(...) ((void)0)
#endif
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 1
#define RA_LOG_DEBUG(logger, ...) RA_LOG_DETAIL(logger, Debug, __VA_ARGS__)
#else
#define RA_LOG_DEBUG(...) ((void)0)
#endif
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 2
#define RA_LOG_INFO(logger, ...) RA_LOG_DETAIL(logger, Info, __VA_ARGS__)
#else
#define RA_LOG_INFO(...) ((void)0)
#endif
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 3
#define RA_LOG_WARNING(logger, ...) RA_LOG_DETAIL(logger, Warning, __VA_ARGS__)
#else
#define RA_LOG_WARNING(...) ((void)0)
#endif
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 4
#define RA_LOG_ERROR(logger, ...) RA_LOG_DETAIL(logger, Error, __VA_ARGS__)
#else
#define RA_LOG_ERROR(...) ((void)0)
#endif
#if RA_ENABLE_LOGGING && RA_LOG_ACTIVE_LEVEL <= 5
#define RA_LOG_CRITICAL(logger, ...) RA_LOG_DETAIL(logger, Critical, __VA_ARGS__)
#else
#define RA_LOG_CRITICAL(...) ((void)0)
#endif
