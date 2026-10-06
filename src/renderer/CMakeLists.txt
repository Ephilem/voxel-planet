file(GLOB_RECURSE RENDERER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(VoxelPlanetRenderer STATIC ${RENDERER_SOURCES})

add_library(VoxelPlanet::Renderer ALIAS VoxelPlanetRenderer)

target_include_directories(VoxelPlanetRenderer
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetRenderer
        PUBLIC
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
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
