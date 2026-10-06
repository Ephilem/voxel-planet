#pragma once

#include <flecs.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#include <atomic>
#include <cstring>
#include <vector>

#define VOXEL_FRAME_MARK FrameMark

#define VOXEL_ZONE ZoneScoped
#define VOXEL_ZONE_N(name) ZoneScopedN(name)
#define VOXEL_ZONE_C(color) ZoneScopedC(color)
#define VOXEL_ZONE_NC(name, color) ZoneScopedNC(name, color)

#define VOXEL_PLOT(name, val) TracyPlot(name, val)

#define VOXEL_MESSAGE(msg) TracyMessage(msg, strlen(msg))

#define VOXEL_LOCKABLE(type, name) TracyLockable(type, name)
#define VOXEL_LOCKABLE_N(type, name, desc) TracyLockableN(type, name, desc)

namespace vp::core::tracy_integration {

//  "flecs.*" internal zone
inline std::atomic<bool> g_trace_flecs_internals{false};

inline thread_local std::vector<TracyCZoneCtx> t_zone_stack;

inline void perf_trace_push(const char* file, size_t line, const char* name) {
    TracyCZoneCtx ctx{};

    const bool internal = std::strncmp(name, "flecs.", 6) == 0;
    const bool wanted = !internal || g_trace_flecs_internals.load(std::memory_order_relaxed);

    if (wanted && ___tracy_connected()) {
        const size_t nameLen = std::strlen(name);
        const uint64_t srcloc = ___tracy_alloc_srcloc_name(static_cast<uint32_t>(line), file, std::strlen(file), name,
                                                           nameLen, name, nameLen, 0);
        ctx = ___tracy_emit_zone_begin_alloc(srcloc, 1);
    }
    t_zone_stack.push_back(ctx);
}

inline void perf_trace_pop(const char*, size_t, const char*) {
    if (t_zone_stack.empty()) {
        return;
    }
    ___tracy_emit_zone_end(t_zone_stack.back());
    t_zone_stack.pop_back();
}

inline void InstallFlecsHooks() {
    ecs_os_set_api_defaults();
    ecs_os_api_t api = ecs_os_get_api();
    api.perf_trace_push_ = perf_trace_push;
    api.perf_trace_pop_ = perf_trace_pop;
    ecs_os_set_api(&api);
}

// The frame mark itself is emitted by the renderer, right after present (Renderer-FrameMark)
inline void Register(flecs::world&) {
    if (!ecs_get_build_info()->perf_trace) {
        ecs_warn("flecs built without FLECS_PERF_TRACE: system zones won't appear in Tracy");
    }
}

} // namespace vp::core::tracy_integration

#else

// No-op macros when Tracy is disabled
#define VOXEL_FRAME_MARK
#define VOXEL_ZONE
#define VOXEL_ZONE_N(name)
#define VOXEL_ZONE_C(color)
#define VOXEL_ZONE_NC(name, color)
#define VOXEL_PLOT(name, val)
#define VOXEL_MESSAGE(msg)
#define VOXEL_LOCKABLE(type, name) type name
#define VOXEL_LOCKABLE_N(type, name, desc) type name

namespace vp::core::tracy_integration {
inline void InstallFlecsHooks() {}

inline void Register(flecs::world&) {}
} // namespace vp::core::tracy_integration

#endif
