add_library(VoxelPlanetPlatform STATIC
    PlatformModule.cpp
    PlatformModule.h
    PlatformState.h
    events.h
    window/Window.cpp
    window/Window.h
)

add_library(VoxelPlanet::Platform ALIAS VoxelPlanetPlatform)

target_include_directories(VoxelPlanetPlatform
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetPlatform
    PUBLIC
        glm::glm
        glfw
        flecs::flecs_static
    PRIVATE
        VoxelPlanet::Core
)
