#pragma once

#include <flecs.h>

#include "utils/common/BaseModule.h"

namespace vp {
class RendererModule : public utils::BaseModule<RendererModule> {
public:
    RendererModule(flecs::world& ecs) : BaseModule(ecs) {}

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<RendererModule>;
};
} // namespace vp