#pragma once
#include "utils/common/BaseModule.h"

namespace vp {

class PlanetClientModule : public utils::BaseModule<PlanetClientModule> {
public:
    PlanetClientModule(flecs::world& ecs) : BaseModule<PlanetClientModule>(ecs) {}

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<PlanetClientModule>;
};
} // namespace vp
