#pragma once

#include "raiders/Profile.h"
#include <filesystem>
#include <memory>
#include <string>

namespace raiders {
enum class TraceContentMode { SerializedEditing, PackagingDebug, Shipping };
struct TraceConfig {
    // Must be a new directory. Never share it with another capture/writer.
    std::filesystem::path directory;
    std::string session_id, process_instance_id, clock_domain_id, build_id, process_name;
    TraceContentMode content_mode = TraceContentMode::SerializedEditing;
    std::uint64_t trace_pid = 1;
    std::size_t events_per_chunk = 128, max_chunks = 32;
    std::size_t max_threads = 128, max_async = 128;
};
struct TraceStats {
    std::uint64_t accepted = 0, dropped = 0, rejected = 0, io_errors = 0;
    std::size_t committed_chunks = 0, pending_events = 0, open_async = 0;
    bool finalized = false;
};

// Consumer-side writer: serialization and file I/O are synchronous. Feed it from
// a capture/drain thread, not an instrumented frame's hot path. Methods serialize
// access; join callers before destruction. Initialization failures throw.
class TraceWriter final {
public:
    explicit TraceWriter(TraceConfig config);
    ~TraceWriter(); // Best-effort checkpoint only; call Finalize explicitly.
    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;
    bool RegisterThread(std::uint64_t thread_id, std::string_view name);
    bool AppendScope(const ProfileEvent& event);
    bool Counter(std::uint64_t thread_id, std::string_view category, std::string_view name,
                 std::string_view series, double value);
    bool AsyncBegin(std::uint64_t thread_id, std::string_view category, std::string_view name,
                    std::string_view id, ProfileContext context = {});
    bool AsyncEnd(std::uint64_t thread_id, std::string_view id);
    // Feed cumulative collector stats after draining; regressions are rejected.
    bool ObserveProfile(const ProfileStats& stats);
    bool Checkpoint();
    bool Finalize(); // Writes directory/trace.json; idempotent, does not overwrite it.
    TraceStats Stats() const;
    // Refuses an active writer, ignores .tmp/orphan blocks, writes recovered.json.
    // Bounded, strict recovery; no global .NET/Python/runtime dependencies.
    static bool Recover(const std::filesystem::path& directory);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
