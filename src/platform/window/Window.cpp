#include "Window.h"

#include <iostream>
#include <stdexcept>

#include "core/log/Logger.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

Window::Window(uint16_t w, uint16_t h, const std::string& t)
    : width(w), height(h), title(t) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    this->window = window;
}

Window::~Window() {
    LOG_DEBUG("Window", "Destroying GLFW window...");
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

glm::ivec2 Window::getFramebufferSize() const {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    return {width, height};
}

void Window::setupCallbacks(flecs::world &ecs) {
    glfwSetWindowUserPointer(window, &ecs);

    glfwSetFramebufferSizeCallback(window,
        [](GLFWwindow* win, int width, int height) {
            auto* world = static_cast<flecs::world*>(glfwGetWindowUserPointer(win));

            WindowResizeEvent evt {
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height)
            };

            world->event<WindowResizeEvent>()
                .ctx(evt)
                .id<PlatformState>()
                .entity(world->id<PlatformState>())
                .enqueue();
        });
}
