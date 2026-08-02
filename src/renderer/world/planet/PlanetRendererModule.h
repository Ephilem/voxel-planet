#pragma once
#include "utils/common/BaseModule.h"

namespace vp {
    class PlanetRendererModule : public utils::BaseModule<PlanetRendererModule> {

    public:
        PlanetRendererModule(flecs::world& ecs): BaseModule(ecs) {}

    private:
        void init_renderers(flecs::world& ecs);

        void register_components(flecs::world& ecs);
        void register_systems(flecs::world& ecs);
        void register_pipelines(flecs::world& ecs);
        void register_submodules(flecs::world& ecs);
        void register_entities(flecs::world& ecs);

        friend class BaseModule;
    };
}
