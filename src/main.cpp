#include "core/CoreModule.h"
#include "platform/PlatformModule.h"
#include "renderer/RendererModule.h"
#include <flecs.h>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        flecs::world ecs{};

        // Import modules in dependency order
        // Core: GameState, ResourceSystem (shared - can run on server)
        // Platform: Window, Input (client only)
        // Renderer: VulkanBackend, ImGui (client only, depends on Platform)
        ecs.import<CoreModule>();
        ecs.import<PlatformModule>();
        ecs.import<RendererModule>();

        while (ecs.progress()) {
            // Main loop
        }

        // Explicit shutdown in reverse order (before world destruction)
        // This ensures Vulkan resources are destroyed before the window
        shutdown_renderer(ecs);
        shutdown_platform(ecs);
        shutdown_core(ecs);

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
