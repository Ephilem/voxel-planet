add_library(VoxelPlanetCore STATIC
        CoreModule.cpp
        CoreModule.h
    GameState.h
    events.h
    log/Logger.cpp
    log/Logger.h
    resource/asset_id.h
    resource/resource_type.h
    resource/AssetRegistry.cpp
    resource/AssetRegistry.h
    resource/loaders/ResourceLoader.h
    resource/loaders/ImageLoader.cpp
    resource/loaders/ImageLoader.h
    resource/ResourceSystem.cpp
    resource/ResourceSystem.h
    resource/loaders/ShaderLoader.cpp
    resource/loaders/ShaderLoader.h
    world/world_components.h
    world/WorldGenerator.cpp
        debug/DebugDrawModule.cpp
        debug/DebugDrawModule.h
    world/WorldGenerator.h
    main_components.h
    world/ChunkManager.cpp
    world/ChunkManager.h
    math/frustrum.h
    math/aabb.h
    physics/physics_components.h
    physics/PhysicsSystem.cpp
    physics/PhysicsSystem.h
    math/utils.h
        debug/DebugDraw.cpp
        debug/DebugDraw.h
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
        fmt::fmt
        FastNoise2
    PRIVATE
        glfw
        imgui
)
