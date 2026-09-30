#include "DebugDrawModule.h"

#include "DebugDraw.h"

using namespace vp;

void DebugDrawModule::register_components(flecs::world& ecs) {
    ecs.component<DebugDrawBuffer>().add(flecs::Singleton);
    ecs.component<DebugDrawBuffer>().set(DebugDrawBuffer{});
    DebugDraw::init(ecs.try_get_mut<DebugDrawBuffer>());
}

void DebugDrawModule::register_systems(flecs::world& ecs) {
    ecs.system("DebugDrawModule-ClearBuffers").kind(flecs::OnLoad).run([](flecs::iter& it) {
        auto* buf = it.world().try_get_mut<DebugDrawBuffer>();
        buf->lines.clear();
        buf->points.clear();
    });
}