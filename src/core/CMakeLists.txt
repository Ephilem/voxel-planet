add_library(VoxelPlanetCore STATIC
        CoreModule.cpp
        CoreModule.h
        window/Window.cpp
        window/Window.h
        Application.cpp
        Application.h
)

add_library(VoxelPlanet::Core ALIAS VoxelPlanetCore)

target_include_directories(VoxelPlanetCore
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetCore
        PUBLIC
            glm::glm
            flecs::flecs_static
        PRIVATE
)