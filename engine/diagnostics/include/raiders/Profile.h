#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#ifndef RA_ENABLE_PROFILING
#define RA_ENABLE_PROFILING 0
#endif

namespace raiders {
namespace detail { struct ProfileState; }

// Input views are copied into bounded event storage at scope entry.
struct ProfileContext {
    std::string_view correlation_id, asset_id, entity_id, widget_id, vm_instance_id;
};
using ProfileText = std::array<char, 129>;
struct ProfileEvent {
    ProfileText name{}, category{};
    ProfileText correlation_id{}, asset_id{}, entity_id{}, widget_id{}, vm_instance_id{};
    std::uint64_t thread_id = 0, timestamp_us = 0, duration_us = 0;
};
struct ProfileStats {
    std::uint64_t accepted = 0, completed = 0, dropped_full = 0, rejected = 0;
    std::size_t in_flight = 0, buffered = 0, capacity = 0;
    bool accepting = false;
};

class ProfileScope;
class ProfileCapture final {
public:
    // Allocates all collector slots/index buffers once. OFF builds allocate none.
    explicit ProfileCapture(std::size_t capacity = 4096);
    ~ProfileCapture();
    ProfileCapture(const ProfileCapture&) = delete;
    ProfileCapture& operator=(const ProfileCapture&) = delete;
    void Stop() noexcept; // Prevent new scopes; existing scopes can still finish.
    std::vector<ProfileEvent> Drain(); // Completed events only; consumer allocation.
    ProfileStats Stats() const;
    static std::uint64_t ClockNowUs(); // Same steady-clock epoch/units as Log.
private:
    friend class ProfileScope;
    std::shared_ptr<detail::ProfileState> state_;
};

// Lexical, same-thread RAII scope; must not be copied or moved across threads.
// Retains capture storage until scope exit, including if the capture is destroyed.
class [[nodiscard]] ProfileScope final {
public:
    ProfileScope(ProfileCapture& capture, std::string_view category, std::string_view name,
                 ProfileContext context = {}) noexcept;
    ~ProfileScope() noexcept;
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;
private:
    std::shared_ptr<detail::ProfileState> state_;
    std::size_t slot_ = static_cast<std::size_t>(-1);
};
}

#define RA_PROFILE_JOIN_IMPL(a, b) a##b
#define RA_PROFILE_JOIN(a, b) RA_PROFILE_JOIN_IMPL(a, b)
#if RA_ENABLE_PROFILING
#define RA_PROFILE_SCOPE(...) \
    ::raiders::ProfileScope RA_PROFILE_JOIN(ra_profile_scope_, __COUNTER__){__VA_ARGS__}
#else
#define RA_PROFILE_SCOPE(...) ((void)0)
#endif
