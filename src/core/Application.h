#pragma once
#include <memory>

#include "window/Window.h"

struct Application {
    std::unique_ptr<Window> window;
    bool isRunning = true;
    double deltaTime = 0.0;
    double lastTime = 0.0;
};
