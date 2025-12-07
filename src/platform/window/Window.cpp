#include "Window.h"

#include <iostream>
#include <stdexcept>

#include "core/log/Logger.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

Window::Window(uint16_t w, uint16_t h, const std::string& t)
    : width(w), height(h), title(t) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
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
    glfwSetWindowUserPointer(window, ecs.c_ptr());

    glfwSetFramebufferSizeCallback(window,
    [](GLFWwindow* win, int width, int height) {
            auto* world_c = static_cast<ecs_world_t*>(glfwGetWindowUserPointer(win));
            flecs::world world(world_c);
            auto platformStateEntity = world.entity<PlatformState>();

            WindowResizeEvent evt {
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height)
            };

            world.event<WindowResizeEvent>()
            .id<PlatformState>()
            .entity(platformStateEntity)
            .ctx(evt)
            .emit();
        });
}
