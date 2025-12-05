add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.h
        vulkan/vulkan_type.inl
        debug/ImGuiManager.h
        debug/ImGuiManager.cpp
        debug/LogConsole.h
        debug/LogConsole.cpp
        debug/IImGuiDebugModule.h
        debug/ImGuiDebugModuleManager.cpp
        debug/ImGuiDebugModuleManager.h
        debug/FpsCounter.cpp
        debug/FpsCounter.h
        debug/VoxelBufferVisualizer.cpp
        debug/VoxelBufferVisualizer.h
        world/VoxelTerrainRenderer.cpp
        world/VoxelTerrainRenderer.h
        world/RenderingComponents.h
        world/VoxelBuffer.cpp
        world/VoxelBuffer.h
        render_types.h
)

add_library(VoxelPlanet::Renderer ALIAS VoxelPlanetRenderer)

target_include_directories(VoxelPlanetRenderer
        PUBLIC
            ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetRenderer
        PUBLIC
            Vulkan::Vulkan
            vk-bootstrap::vk-bootstrap
            VulkanMemoryAllocator
            glm::glm
            nvrhi_vk
            nvrhi
            imgui
            flecs::flecs_static
        PRIVATE
            VoxelPlanet::Core
            VoxelPlanet::Platform
)