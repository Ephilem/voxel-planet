add_library(VoxelPlanetRenderer STATIC
        vulkan/VulkanBackend.cpp
        vulkan/VulkanBackend.h
        RendererModule.cpp
        RendererModule.h
        Renderer.cpp
        Renderer.h
        vulkan/VulkanPipeline.cpp
        vulkan/VulkanPipeline.h
        vulkan/VulkanRenderPass.cpp
        vulkan/VulkanRenderPass.h
        vulkan/vulkan_type.inl
        vulkan/VulkanSwapchain.cpp
        vulkan/VulkanSwapchain.h
        vulkan/vma_implementation.cpp
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
            flecs::flecs_static
        PRIVATE
)