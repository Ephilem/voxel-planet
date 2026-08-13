#include "InputModule.h"

#include "core/TracyIntegration.h"
#include "InputModuleState.h"
#include "platform/PlatformState.h"

using namespace vp;

void InputModule::register_components(flecs::world& ecs) {
    auto* platformState = ecs.get<PlatformState>();
    if (!platformState || !platformState->window)
        throw std::runtime_error("InputModule: PlatformState with valid Window is required");

    ecs.component<InputState>();
    ecs.component<InputActionState>();
    ecs.component<InputModuleState>();

    ecs.set<InputState>({});
    ecs.set<InputActionState>({});
    ecs.set<InputModuleState>({.inputManager = std::make_unique<InputStateManager>()});
}

void InputModule::register_systems(flecs::world& ecs) {
    ecs.system("CaptureInputSystem").kind(flecs::OnLoad).run([](flecs::iter& it) {
        VOXEL_ZONE_N("InputStateManager-CaptureInput");
        auto* inputState = it.world().get_mut<InputState>();
        auto* platform = it.world().get_mut<PlatformState>();
        if (inputState && platform)
            InputStateManager::capture_input_system(*inputState, *platform);
    });

    ecs.system("UpdateActionStatesSystem").kind(flecs::PreUpdate).run([](flecs::iter& it) {
        VOXEL_ZONE_N("InputStateManager-UpdateAction");
        auto* inputState = it.world().get_mut<InputState>();
        auto* actionState = it.world().get_mut<InputActionState>();
        auto* moduleState = it.world().get_mut<InputModuleState>();
        auto* platform = it.world().get_mut<PlatformState>();
        if (inputState && actionState && moduleState && platform)
            InputStateManager::update_action_states_system(*inputState, *actionState, *moduleState, *platform->window);
    });
}
