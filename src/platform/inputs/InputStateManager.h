#pragma once

#include <flecs.h>
#include <unordered_map>
#include <vector>

#include "input_state.h"

struct PlatformState;

struct KeyBinding {
    int key; // GLFW key code or mouse button code
    int modifier; // GLFW modifier mask (e.g., GLFW_MOD_SHIFT)
    bool isMouse = false;

    KeyBinding() = default;
    explicit KeyBinding(int k, int mods = 0) : key(k), modifier(mods), isMouse(false) {}

    static KeyBinding Mouse(int btn, int mods) {
        KeyBinding kb;
        kb.key = btn;
        kb.modifier = mods;
        kb.isMouse = true;
        return kb;
    }
};

class InputStateManager {
    std::unordered_map<ActionInputType, std::vector<KeyBinding>> m_actionKeyBindings;

    float m_mouseSensitivityX = 1.0f;
    float m_mouseSensitivityY = 1.0f;


public:
    InputStateManager();
    ~InputStateManager();

    void bind_action(ActionInputType action, int key, int modifier = 0);
    void bind_action_mouse(ActionInputType action, int mouseButton, int modifier = 0);
    void unbind_action(ActionInputType action, int key, int modifier = 0);
    void clear_action_bindings(ActionInputType action);

    bool is_binding_active(const KeyBinding& binding, const InputState& inputState) const;

    static void Register(flecs::world& ecs);

    // -- ECS --
    static void capture_input_system(InputState &inputState, PlatformState& platformState);
    static void update_action_states_system(InputState &inputState, InputActionState &actionState, PlatformState& platformState);
};
