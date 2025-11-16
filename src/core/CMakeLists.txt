add_library(VoxelPlanetCore STATIC
        CoreModule.cpp
        CoreModule.h
        window/Window.cpp
        window/Window.h
        Application.cpp
        Application.h
        events.h
        resource/asset_id.h
        resource/resource_type.h
        resource/loaders/ResourceLoader.h
        resource/loaders/ImageLoader.cpp
        resource/loaders/ImageLoader.h
        resource/ResourceSystem.cpp
        resource/ResourceSystem.h
)

add_library(VoxelPlanet::Core ALIAS VoxelPlanetCore)

target_include_directories(VoxelPlanetCore
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(VoxelPlanetCore
        PUBLIC
            glm::glm
            stb
            flecs::flecs_static
        PRIVATE
)