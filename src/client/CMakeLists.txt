add_library(VoxelPlanetClient STATIC
        ClientModule.cpp
        ClientModule.h)

add_library(VoxelPlanet::Client ALIAS VoxelPlanetClient)

target_include_directories(VoxelPlanetClient
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetClient
        PUBLIC
        glm::glm
        flecs::flecs_static
        PRIVATE
        VoxelPlanet::Core
        VoxelPlanet::Platform
        VoxelPlanet::Renderer
)