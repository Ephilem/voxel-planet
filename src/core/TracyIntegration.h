#pragma once

#include <flecs.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>

#define VOXEL_FRAME_MARK FrameMark

#define VOXEL_ZONE ZoneScoped
#define VOXEL_ZONE_N(name) ZoneScopedN(name)
#define VOXEL_ZONE_C(color) ZoneScopedC(color)
#define VOXEL_ZONE_NC(name, color) ZoneScopedNC(name, color)

#define VOXEL_PLOT(name, val) TracyPlot(name, val)

#define VOXEL_MESSAGE(msg) TracyMessage(msg, strlen(msg))

#define VOXEL_LOCKABLE(type, name) TracyLockable(type, name)
#define VOXEL_LOCKABLE_N(type, name, desc) TracyLockableN(type, name, desc)

namespace tracy_integration {

// Register Tracy systems with Flecs
inline void Register(flecs::world& ecs) {
    // FrameMark system - runs at the very end of each frame
    ecs.system("Tracy_FrameMark")
        .kind(flecs::PostFrame)
        .run([](flecs::iter& it) {
            (void)it;
            FrameMark;
        });
}

} // namespace tracy_integration

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

namespace tracy_integration {
inline void Register(flecs::world&) {}
}

#endif