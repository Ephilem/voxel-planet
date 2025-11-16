//
// Created by raph on 08/11/2025.
//

#include "CoreModule.h"
#include "Application.h"
#include <GLFW/glfw3.h>
#include <iostream>

#include "events.h"

CoreModule::CoreModule(flecs::world& ecs) {

    ecs.component<WindowResizeEvent>();

    ecs.set<Application>({
        .window = std::make_unique<Window>(1280, 720, "VoxelPlanet"),
        .resourceSystem = std::make_unique<ResourceSystem>(),
        .isRunning = true,
        .deltaTime = 0.0,
        .lastTime = glfwGetTime()
    });

    ecs.system<Application>()
        .kind(flecs::PreUpdate)
        .each([](flecs::iter& it, size_t, Application& app) {
            app.window->pollEvents();

            double currentTime = glfwGetTime();
            app.deltaTime = currentTime - app.lastTime;
            app.lastTime = currentTime;

            if (app.window->shouldClose()) {
                app.isRunning = false;
            }

            if (glfwGetKey(app.window->window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                std::cout << "ESC pressed, initiating shutdown..." << std::endl;
                app.isRunning = false;
            }
        });

    ecs.system<Application>()
        .kind(flecs::PostUpdate)
        .each([](flecs::iter& it, size_t, Application& app) {
            if (!app.isRunning) {
                std::cout << "CoreModule: Shutdown requested, quitting ECS world..." << std::endl;
                it.world().quit();
            }
        });

    ecs.system<Application>()
        .kind(flecs::PostUpdate)
        .interval(1.0f)
        .each([](flecs::iter& it, size_t, Application& app) {
            double fps = 1.0 / app.deltaTime;
            std::cout << "FPS: " << static_cast<int>(fps) << ", "
                      << "Delta: " << static_cast<int>(app.deltaTime * 1000) << "ms" << std::endl;
        });


    ecs.get_mut<Application>()->window->setupCallbacks(ecs);
}

CoreModule::~CoreModule() = default;
