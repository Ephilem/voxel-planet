add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.cpp
        Renderer.h
        vulkan/vulkan_type.inl
        managers/ImGuiManager.h
        managers/ImGuiManager.cpp
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