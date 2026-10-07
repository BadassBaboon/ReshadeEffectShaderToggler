#pragma once

// Opt-in diagnostic logging ("DiagnosticLogs=True" in the [General] section of
// ReshadeEffectShaderToggler.ini). Off by default so regular users' ReShade.log stays clean.
//
// Two kinds of output:
//  - Events (group toggles, effect reloads, buffer recreation) are logged immediately.
//  - Per-render details are logged only for a few frames after something changes, and a
//    once-per-second summary of counters is logged at present. This keeps the log readable
//    while still showing what happens every frame.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <reshade.hpp>
#include <string>

namespace RestDiag {
inline std::atomic_bool g_enabled{ false };

// Frames left during which per-render detail lines are logged.
inline std::atomic_int g_detailFrames{ 0 };
constexpr int DETAIL_FRAMES_AFTER_CHANGE = 3;

struct Counters {
    std::atomic_uint64_t frames{ 0 };
    std::atomic_uint64_t renderEffectsCalls{ 0 };   // RenderEffects invocations that rendered something
    std::atomic_uint64_t techniquesRendered{ 0 };   // render_technique calls for group techniques
    std::atomic_uint64_t stagedRenders{ 0 };        // group renders that went through the native staging copy
    std::atomic_uint64_t directRenders{ 0 };        // group renders straight into the game's target
    std::atomic_uint64_t mismatchedDirectRenders{ 0 }; // renders whose effect target differs from the swapchain in size or format
    std::atomic_uint64_t skippedRenders{ 0 };       // group renders skipped (buffers not ready, unsupported target...)
    std::atomic_uint64_t groupBufferRecreations{ 0 };
    std::atomic_uint64_t effectReloads{ 0 };
    // CPU time (microseconds) spent inside REST's calls, to tell CPU-side stalls from GPU cost.
    std::atomic_uint64_t techniqueCpuMicros{ 0 };    // runtime->render_technique for group techniques
    std::atomic_uint64_t techniqueCpuMaxMicros{ 0 };
    std::atomic_uint64_t stagingCpuMicros{ 0 };      // staging copy in + copy back
    std::atomic_uint64_t renderEffectsCpuMicros{ 0 }; // whole RenderEffects, including the above
    std::atomic_uint64_t presentCpuMicros{ 0 };      // REST's work in reshade_present
};

// Measures CPU time of a scope into a counter (only when diagnostics are on).
struct ScopedCpuTimer {
    std::atomic_uint64_t* target;
    std::atomic_uint64_t* maxTarget;
    std::chrono::steady_clock::time_point start;
    explicit ScopedCpuTimer(std::atomic_uint64_t& counter, std::atomic_uint64_t* maxCounter = nullptr)
      : target(g_enabled.load(std::memory_order_relaxed) ? &counter : nullptr), maxTarget(maxCounter) {
        if (target != nullptr)
            start = std::chrono::steady_clock::now();
    }
    uint64_t ElapsedMicros() const {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    }
    ~ScopedCpuTimer() {
        if (target == nullptr)
            return;
        const uint64_t us = ElapsedMicros();
        target->fetch_add(us, std::memory_order_relaxed);
        if (maxTarget != nullptr) {
            uint64_t prev = maxTarget->load(std::memory_order_relaxed);
            while (us > prev && !maxTarget->compare_exchange_weak(prev, us, std::memory_order_relaxed)) {}
        }
    }
};
inline Counters g_counters;

inline bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }
inline bool DetailEnabled() { return Enabled() && g_detailFrames.load(std::memory_order_relaxed) > 0; }
inline void RequestDetail() { g_detailFrames.store(DETAIL_FRAMES_AFTER_CHANGE, std::memory_order_relaxed); }

template <typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args) {
    if (!Enabled())
        return;
    const std::string message = "[REST][diag] " + std::format(fmt, std::forward<Args>(args)...);
    reshade::log::message(reshade::log::level::info, message.c_str());
}

template <typename... Args>
void Detail(std::format_string<Args...> fmt, Args&&... args) {
    if (!DetailEnabled())
        return;
    Log(fmt, std::forward<Args>(args)...);
}

inline void Count(std::atomic_uint64_t& counter, uint64_t amount = 1) {
    if (Enabled())
        counter.fetch_add(amount, std::memory_order_relaxed);
}

// Called once per present. Ages the detail window and logs a per-second summary.
inline void OnPresent() {
    if (!Enabled())
        return;

    const int detail = g_detailFrames.load(std::memory_order_relaxed);
    if (detail > 0)
        g_detailFrames.store(detail - 1, std::memory_order_relaxed);

    g_counters.frames.fetch_add(1, std::memory_order_relaxed);

    using clock = std::chrono::steady_clock;
    static clock::time_point s_windowStart = clock::now();
    const auto now = clock::now();
    const double seconds = std::chrono::duration<double>(now - s_windowStart).count();
    // Also sample one frame of per-render detail every few seconds, so steady state is visible.
    static int s_windowsSinceDetail = 0;
    if (seconds < 1.0)
        return;
    s_windowStart = now;
    if (++s_windowsSinceDetail >= 3) {
        s_windowsSinceDetail = 0;
        if (g_detailFrames.load(std::memory_order_relaxed) <= 0)
            g_detailFrames.store(1, std::memory_order_relaxed);
    }

    const uint64_t frames = g_counters.frames.exchange(0);
    const double perFrame = frames > 0 ? 1.0 / (frames * 1000.0) : 0.0; // micros -> ms per frame
    Log("summary {:.1f}s: {} frames ({:.1f} fps, {:.2f} ms/frame) | RenderEffects calls={} techniques={} | staged={} direct={} swapchain-mismatched={} skipped={} | "
        "group buffer recreations={} | ReShade effect reloads={} | CPU ms/frame: RenderEffects={:.3f} render_technique={:.3f} (max single {:.3f}) staging={:.3f} present={:.3f}",
        seconds,
        frames,
        frames / seconds,
        frames > 0 ? seconds * 1000.0 / frames : 0.0,
        g_counters.renderEffectsCalls.exchange(0),
        g_counters.techniquesRendered.exchange(0),
        g_counters.stagedRenders.exchange(0),
        g_counters.directRenders.exchange(0),
        g_counters.mismatchedDirectRenders.exchange(0),
        g_counters.skippedRenders.exchange(0),
        g_counters.groupBufferRecreations.exchange(0),
        g_counters.effectReloads.exchange(0),
        g_counters.renderEffectsCpuMicros.exchange(0) * perFrame,
        g_counters.techniqueCpuMicros.exchange(0) * perFrame,
        g_counters.techniqueCpuMaxMicros.exchange(0) / 1000.0,
        g_counters.stagingCpuMicros.exchange(0) * perFrame,
        g_counters.presentCpuMicros.exchange(0) * perFrame);
}
}
