add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.h
        vulkan/vulkan_type.inl
        debug/ImGuiManager.h
        debug/ImGuiManager.cpp
        debug/DebugImageTexture.h
        debug/DebugImageTexture.cpp
        render_types.h
        IRenderPass.h
        world/VoxelTextureManager.cpp
        world/VoxelTextureManager.h
        TracyVulkanIntegration.h
        debug/DebugDrawRenderer.cpp
        debug/DebugDrawRenderer.h
        camera/Camera3dModule.cpp
        camera/Camera3dModule.h
        camera/camera3d_systems.h
        debug/DebugRenderModule.cpp
        debug/DebugRenderModule.h
        world/planet/PlanetRendererModule.cpp
        world/planet/PlanetRendererModule.h
        world/planet/PlanetTileRenderer.cpp
        world/planet/PlanetTileRenderer.h
        world/planet/planet_rendering_components.h
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
            implot
            flecs::flecs_static
            concurrentqueue
        PRIVATE
            VoxelPlanet::Core
            VoxelPlanet::Platform
            VoxelPlanet::Utils
)