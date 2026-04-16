#pragma once

#include <flecs.h>
#include "utils/common/BaseModule.h"

namespace vp {

    class PlanetModule : public utils::BaseModule<PlanetModule> {
    public:
        PlanetModule(flecs::world& ecs) : BaseModule<PlanetModule>(ecs) {}

    private:
        void register_components(flecs::world& ecs);
        void register_systems(flecs::world& ecs);
        void register_pipelines(flecs::world& ecs);
        void register_submodules(flecs::world& ecs);
        void register_entities(flecs::world& ecs);

        friend class BaseModule<PlanetModule>;
    };
}
