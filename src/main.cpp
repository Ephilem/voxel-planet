#include "core/CoreModule.h"
#include <flecs.h>
#include "renderer/RendererModule.h"
#include "core/Application.h"
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    try {
        flecs::world ecs;

        ecs.import<CoreModule>();
        ecs.import<RendererModule>();


        while (ecs.progress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }


    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
