#pragma once
#include <memory>

#include "resource/ResourceSystem.h"
#include "window/Window.h"

struct Application {
    // Systems
    std::unique_ptr<Window> window;
    std::unique_ptr<ResourceSystem> resourceSystem;

    bool isRunning = true;
    double deltaTime = 0.0;
    double lastTime = 0.0;
};
