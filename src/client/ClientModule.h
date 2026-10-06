#pragma once

#include <flecs.h>

#include "core/ecs/BaseModule.h"

namespace vp::client {
class ClientModule : public core::BaseModule<ClientModule> {
public:
    ClientModule(flecs::world& ecs) : BaseModule(ecs) { register_all(ecs); }

private:
    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);
    void register_pipelines(flecs::world& ecs);
    void register_submodules(flecs::world& ecs);
    void register_entities(flecs::world& ecs);

    friend class BaseModule<ClientModule>;
};
} // namespace vp::client
