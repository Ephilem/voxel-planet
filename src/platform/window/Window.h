#pragma once
#include <string>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <flecs.h>

struct Window {
    uint16_t width;
    uint16_t height;
    std::string title;

    GLFWwindow* window;

    Window(uint16_t w, uint16_t h, const std::string& t);
    ~Window();

    bool shouldClose() const;
    void pollEvents() const;
    glm::ivec2 getFramebufferSize() const;

    void setupCallbacks(flecs::world& ecs);
};
