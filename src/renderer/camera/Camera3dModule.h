#pragma once

#include "utils/common/BaseModule.h"

namespace vp {
class Camera3dModule : public utils::BaseModule<Camera3dModule> {
public:
    Camera3dModule(flecs::world& ecs) : BaseModule(ecs) {}

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);

    friend class BaseModule;
};
} // namespace vp
