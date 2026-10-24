#include "raiders/Profile.h"

#include <chrono>

#if RA_ENABLE_PROFILING
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace raiders {
namespace {
constexpr auto noSlot = static_cast<std::size_t>(-1);
constexpr std::uint64_t jsonIntegerMax = 9007199254740991ULL;
std::atomic<std::uint64_t> nextThreadId{1};
std::uint64_t ThreadId() noexcept {
    // Logical thread lifetime identity, independent of reusable Windows thread IDs.
    thread_local const auto id = nextThreadId.fetch_add(1, std::memory_order_relaxed);
    return id;
}
bool CopyText(ProfileText& target, std::string_view source, bool identity = false) noexcept {
    if (source.size() >= target.size()) return false;
    if (!source.empty()) {
        if (source.find('\0') != std::string_view::npos ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                static_cast<int>(source.size()), nullptr, 0)) return false;
        if (identity) {
            const auto alnum = [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            };
            if (!alnum(source.front())) return false;
            for (unsigned char c : source) {
                if (!alnum(c) && c != '.' && c != '_' && c != ':' && c != '/' && c != '-') return false;
            }
        }
        std::copy(source.begin(), source.end(), target.begin());
    }
    return true;
}
}

namespace detail {
struct ProfileState {
    explicit ProfileState(std::size_t capacity) : slots(capacity) {
        free.reserve(capacity);
        ready.reserve(capacity);
        for (std::size_t i = capacity; i > 0; --i) free.push_back(i - 1);
    }
    std::mutex mutex;
    std::vector<ProfileEvent> slots;
    std::vector<std::size_t> free, ready;
    std::uint64_t accepted = 0, completed = 0, dropped_full = 0;
    std::atomic<std::uint64_t> rejected{0};
    bool accepting = true;
};
}

ProfileCapture::ProfileCapture(std::size_t capacity) {
    if (!capacity) throw std::invalid_argument("Profiling capacity must be positive");
    state_ = std::make_shared<detail::ProfileState>(capacity);
}
ProfileCapture::~ProfileCapture() { Stop(); }
void ProfileCapture::Stop() noexcept {
    try { std::lock_guard lock(state_->mutex); state_->accepting = false; }
    catch (...) { ++state_->rejected; }
}
std::vector<ProfileEvent> ProfileCapture::Drain() {
    std::lock_guard lock(state_->mutex);
    std::vector<ProfileEvent> result;
    result.reserve(state_->ready.size()); // Failure leaves the collector untouched.
    for (auto slot : state_->ready) {
        result.push_back(state_->slots[slot]);
        state_->free.push_back(slot);
    }
    state_->ready.clear();
    return result;
}
ProfileStats ProfileCapture::Stats() const {
    std::lock_guard lock(state_->mutex);
    return {state_->accepted, state_->completed, state_->dropped_full, state_->rejected,
        state_->slots.size() - state_->free.size() - state_->ready.size(),
        state_->ready.size(), state_->slots.size(), state_->accepting};
}

ProfileScope::ProfileScope(ProfileCapture& capture, std::string_view category,
                          std::string_view name, ProfileContext context) noexcept
    : state_(capture.state_) {
    try {
        ProfileEvent event;
        event.timestamp_us = ProfileCapture::ClockNowUs();
        if (category.empty() || name.empty() ||
            !CopyText(event.category, category) || !CopyText(event.name, name) ||
            !CopyText(event.correlation_id, context.correlation_id, true) ||
            !CopyText(event.asset_id, context.asset_id, true) ||
            !CopyText(event.entity_id, context.entity_id, true) ||
            !CopyText(event.widget_id, context.widget_id, true) ||
            !CopyText(event.vm_instance_id, context.vm_instance_id, true) ||
            event.timestamp_us > jsonIntegerMax) {
            ++state_->rejected;
            return;
        }
        std::lock_guard lock(state_->mutex);
        if (!state_->accepting) { ++state_->rejected; return; }
        if (state_->free.empty()) { ++state_->dropped_full; return; }
        event.thread_id = ThreadId();
        if (event.thread_id > jsonIntegerMax) { ++state_->rejected; return; }
        slot_ = state_->free.back();
        state_->free.pop_back();
        state_->slots[slot_] = event;
        ++state_->accepted;
    } catch (...) { ++state_->rejected; }
}
ProfileScope::~ProfileScope() noexcept {
    if (slot_ == noSlot) return;
    const auto end = ProfileCapture::ClockNowUs(); // Producer time, before collector lock.
    try {
        std::lock_guard lock(state_->mutex);
        auto& event = state_->slots[slot_];
        if (ThreadId() != event.thread_id || end < event.timestamp_us ||
            end - event.timestamp_us > jsonIntegerMax) {
            ++state_->rejected;
            state_->free.push_back(slot_);
            return;
        }
        event.duration_us = end - event.timestamp_us;
        state_->ready.push_back(slot_); // Capacity reserved at capture creation.
        ++state_->completed;
    } catch (...) { ++state_->rejected; }
}
}
#else
namespace raiders {
ProfileCapture::ProfileCapture(std::size_t) {}
ProfileCapture::~ProfileCapture() = default;
void ProfileCapture::Stop() noexcept {}
std::vector<ProfileEvent> ProfileCapture::Drain() { return {}; }
ProfileStats ProfileCapture::Stats() const { return {}; }
ProfileScope::ProfileScope(ProfileCapture&, std::string_view, std::string_view, ProfileContext) noexcept {}
ProfileScope::~ProfileScope() noexcept = default;
}
#endif

std::uint64_t raiders::ProfileCapture::ClockNowUs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
