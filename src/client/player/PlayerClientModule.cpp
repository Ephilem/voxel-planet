#include "PlayerClientModule.h"

#include "PlayerControllerSystem.h"
#include "player_components.h"
#include "core/physics/physics_components.h"

using namespace vp;

void PlayerClientModule::register_components(flecs::world &ecs) {
    ecs.component<PlayerClient>();
    ecs.component<PlayerController>();
}

void PlayerClientModule::register_systems(flecs::world &ecs) {
    PlayerControllerSystem::Register(ecs);
}

void PlayerClientModule::register_pipelines(flecs::world &ecs) {
}

void PlayerClientModule::register_submodules(flecs::world &ecs) {
}

void PlayerClientModule::register_entities(flecs::world &ecs) {
}
