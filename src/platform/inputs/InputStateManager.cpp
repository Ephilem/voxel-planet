//
// Created by raph on 05/12/2025.
//

#include "InputStateManager.h"

#include "core/log/Logger.h"
#include "platform/PlatformState.h"

InputStateManager::InputStateManager() {
    bind_action(ActionInputType::DebugMenuBar, GLFW_KEY_F3);
    bind_action(ActionInputType::ToggleMouseCapture, GLFW_KEY_LEFT_ALT);
}

InputStateManager::~InputStateManager() {
}

void InputStateManager::bind_action(ActionInputType action, int key, int modifier) {
    LOG_DEBUG("InputStateManager", "Binding action {} to key {} with modifier {}", static_cast<int>(action), key, modifier);
    m_actionKeyBindings[action].emplace_back(key, modifier);
}

void InputStateManager::bind_action_mouse(ActionInputType action, int mouseButton, int modifier) {
    m_actionKeyBindings[action].emplace_back(mouseButton, modifier);
}

void InputStateManager::unbind_action(ActionInputType action, int key, int modifier) {
}

void InputStateManager::clear_action_bindings(ActionInputType action) {
    m_actionKeyBindings[action].clear();
}

bool InputStateManager::is_binding_active(const KeyBinding &binding, const InputState &inputState) const {
    if (binding.isMouse) {
        return inputState.is_mouse_button_down(binding.key);
    } else {
        return inputState.is_key_down(binding.key);
    }
}

void InputStateManager::set_mouse_captured(GLFWwindow* window, InputState& inputState, bool captured) {
    if (inputState.mouseCaptured == captured) return;

    inputState.mouseCaptured = captured;

    if (captured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }

    inputState.mouseDeltaX = 0.0f;
    inputState.mouseDeltaY = 0.0f;
}

void InputStateManager::Register(flecs::world &ecs) {
    ecs.set<InputState>({});
    ecs.set<InputActionState>({});

    auto* platformState = ecs.get_mut<PlatformState>();
    if (!platformState || !platformState->window) {
        throw std::runtime_error("InputStateManager: PlatformState with valid Window is required");
    }
    platformState->inputManager = std::make_unique<InputStateManager>();

    ecs.system("CaptureInputSystem")
        .kind(flecs::OnLoad)
        .run([](flecs::iter& it) {
            auto* inputState = it.world().get_mut<InputState>();
            auto* platformState = it.world().get_mut<PlatformState>();
            if (inputState && platformState) {
                capture_input_system(*inputState, *platformState);
            }
        });

    ecs.system("UpdateActionStatesSystem")
        .kind(flecs::PreUpdate)
        .run([](flecs::iter& it) {
            auto* inputState = it.world().get_mut<InputState>();
            auto* actionState = it.world().get_mut<InputActionState>();
            auto* platformState = it.world().get_mut<PlatformState>();
            if (inputState && actionState && platformState) {
                update_action_states_system(*inputState, *actionState, *platformState);
            }
        });
}

void InputStateManager::capture_input_system(InputState& inputState, PlatformState& platformState) {
    GLFWwindow* window = platformState.window->window;
    // Handle transition
    for (int i = 0; i <= GLFW_KEY_LAST; ++i) {
        if (inputState.keys[i] == KeyState::Pressed) {
            inputState.keys[i] = KeyState::Down;
        } else if (inputState.keys[i] == KeyState::Released) {
            inputState.keys[i] = KeyState::Up;
        }
    }

    for (int btn = 0; btn <= GLFW_MOUSE_BUTTON_LAST; ++btn) {
        if (inputState.mouseButtons[btn] == KeyState::Pressed) {
            inputState.mouseButtons[btn] = KeyState::Down;
        } else if (inputState.mouseButtons[btn] == KeyState::Released) {
            inputState.mouseButtons[btn] = KeyState::Up;
        }
    }

    // Update
    for (int i = 0; i <= GLFW_KEY_LAST; ++i) {
        int state = glfwGetKey(window, i);
        if (state == GLFW_PRESS) {
            if (inputState.keys[i] == KeyState::Up) {
                inputState.keys[i] = KeyState::Pressed;
            }
        } else {
            if (inputState.keys[i] == KeyState::Down) {
                inputState.keys[i] = KeyState::Released;
            }
        }
    }

    // Mouse update
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    inputState.mouseDeltaX = static_cast<float>(mouseX) - inputState.mouseX;
    inputState.mouseDeltaY = static_cast<float>(mouseY) - inputState.mouseY;
    inputState.mouseX = static_cast<float>(mouseX);
    inputState.mouseY = static_cast<float>(mouseY);

    // Mouse buttons
    for (int btn = 0; btn <= GLFW_MOUSE_BUTTON_LAST; ++btn) {
        int state = glfwGetMouseButton(window, btn);
        if (state == GLFW_PRESS) {
            if (inputState.mouseButtons[btn] == KeyState::Up) {
                inputState.mouseButtons[btn] = KeyState::Pressed;
            }
        } else {
            if (inputState.mouseButtons[btn] == KeyState::Down) {
                inputState.mouseButtons[btn] = KeyState::Released;
            }
        }
    }

    inputState.scrollDeltaX = 0.0f;
    inputState.scrollDeltaY = 0.0f;
}

void InputStateManager::update_action_states_system(InputState &inputState, InputActionState &actionState, PlatformState &platformState) {
    auto* inputManager = platformState.inputManager.get();
    if (!inputManager) return;

    GLFWwindow* window = platformState.window->window;

    // Handle transitions
    for (KeyState & action : actionState.actions) {
        if (action == KeyState::Pressed) {
            action = KeyState::Down;
        } else if (action == KeyState::Released) {
            action = KeyState::Up;
        }
    }

    // Update based on bindings
    for (const auto& [action, bindings] : inputManager->m_actionKeyBindings) {
        bool isActive = false;
        for (const auto& binding : bindings) {
            if (inputManager->is_binding_active(binding, inputState)) {
                isActive = true;
                break;
            }
        }

        KeyState& currentState = actionState.actions[static_cast<size_t>(action)];
        if (isActive) {
            if (currentState == KeyState::Up) {
                currentState = KeyState::Pressed;
            }
        } else {
            if (currentState == KeyState::Down) {
                currentState = KeyState::Released;
            }
        }
    }

    if (actionState.is_action_pressed(ActionInputType::ToggleMouseCapture)) {
        inputManager->set_mouse_captured(window, inputState, !inputState.mouseCaptured);
        LOG_TRACE("InputStateManager", "Mouse capture toggled: {}", inputState.mouseCaptured);
    }
}
