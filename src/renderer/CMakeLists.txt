add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.h
        vulkan/vulkan_type.inl
        debug/ImGuiManager.h
        debug/ImGuiManager.cpp
        world/VoxelTerrainRenderer.cpp
        world/VoxelTerrainRenderer.h
        world/VoxelBuffer.cpp
        world/VoxelBuffer.h
        render_types.h
        IRenderPass.h
        world/VoxelTextureManager.cpp
        world/VoxelTextureManager.h
        world/VoxelChunkMesher.cpp
        world/VoxelChunkMesher.h
        TracyVulkanIntegration.h
        world/VoxelMeshUploadBatcher.cpp
        world/VoxelMeshUploadBatcher.h
        debug/DebugDrawRenderer.cpp
        debug/DebugDrawRenderer.h
        world/planet/PlanetOctree.cpp
        world/planet/PlanetOctree.h
        world/planet/PlanetDebugRenderer.cpp
        world/planet/PlanetDebugRenderer.h
        camera/Camera3dModule.cpp
        camera/Camera3dModule.h
        camera/camera3d_systems.h
        debug/DebugRenderModule.cpp
        debug/DebugRenderModule.h
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
        PRIVATE
            VoxelPlanet::Core
            VoxelPlanet::Platform
            VoxelPlanet::Utils
)