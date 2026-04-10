#pragma once

#include <flecs.h>

#include "core/world/world_components.h"
#include "renderer/rendering_components.h"
#include "utils/common/BaseModule.h"

namespace vp {
    class PlanetRenderModule : public utils::BaseModule<PlanetRenderModule> {
    public:
        PlanetRenderModule(flecs::world& ecs): BaseModule(ecs){}

    private:
        void register_components(flecs::world& ecs);
        void register_systems(flecs::world& ecs);
        void register_pipelines(flecs::world& ecs);
        void register_submodules(flecs::world& ecs);
        void register_entities(flecs::world& ecs);

        friend class BaseModule<PlanetRenderModule>;
    };
}
