add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.h
        vulkan/vulkan_type.inl
        debug/ImGuiManager.h
        debug/ImGuiManager.cpp
        world/SkyRenderer.cpp
        world/SkyRenderer.h
        world/VoxelTerrainRenderer.cpp
        world/VoxelTerrainRenderer.h
        world/VoxelBuffer.cpp
        world/VoxelBuffer.h
        render_types.h
        IRenderPass.h
        Camera3dSystems.cpp
        Camera3dSystems.h
        world/VoxelTextureManager.cpp
        world/VoxelTextureManager.h
        world/VoxelChunkMesher.cpp
        world/VoxelChunkMesher.h
        TracyVulkanIntegration.h
        world/VoxelMeshUploadBatcher.cpp
        world/VoxelMeshUploadBatcher.h
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