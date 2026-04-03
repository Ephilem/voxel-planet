#pragma once

#include "core/ecs/BaseModule.h"

namespace vp::client {
class Camera3dModule : public core::BaseModule<Camera3dModule> {
public:
    Camera3dModule(flecs::world& ecs) : BaseModule(ecs) { register_all(ecs); }

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp::client
