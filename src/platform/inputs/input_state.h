#pragma once
#include <GLFW/glfw3.h>

enum class ActionInputType {
    DebugMenuBar,
    ToggleMouseCapture,

    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down,

    Last,
};

enum class KeyState {
    Up = 0,
    Down = 1,
    Pressed = 2,
    Released = 3,
};

struct InputState {
    KeyState keys[GLFW_KEY_LAST + 1];
    KeyState mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1];

    float mouseX, mouseY;
    float mouseDeltaX, mouseDeltaY;
    float scrollDeltaX, scrollDeltaY;

    bool mouseCaptured = false;

    inline bool is_key_down(int key) const {
        return keys[key] == KeyState::Down || keys[key] == KeyState::Pressed;
    }

    inline bool is_key_pressed(int key) const {
        return keys[key] == KeyState::Pressed;
    }

    inline bool is_key_released(int key) const {
        return keys[key] == KeyState::Released;
    }

    inline bool is_mouse_button_down(int button) const {
        return mouseButtons[button] == KeyState::Down || mouseButtons[button] == KeyState::Pressed;
    }
};

struct InputActionState {
    KeyState actions[static_cast<size_t>(ActionInputType::Last)] = { KeyState::Up };

    inline bool is_action_active(ActionInputType action) const {
        return actions[static_cast<size_t>(action)] == KeyState::Down || actions[static_cast<size_t>(action)] == KeyState::Pressed;
    }

    inline bool is_action_pressed(ActionInputType action) const {
        return actions[static_cast<size_t>(action)] == KeyState::Pressed;
    }
};