add_library(VoxelPlanetClient STATIC
        ClientModule.cpp
        ClientModule.h
        player/PlayerControllerSystem.cpp
        player/PlayerControllerSystem.h
        debug/IDebugPanel.h
        debug/DebugUIManager.cpp
        debug/DebugUIManager.h
        debug/LogConsole.h
        debug/LogConsole.cpp
        debug/world/PlayerWorldInfo.h
        debug/world/PlayerWorldInfo.cpp
        debug/performance/FpsCounter.h
        debug/performance/FpsCounter.cpp
        debug/entities/PlayerPanel.cpp
        debug/entities/PlayerPanel.h
        debug/DebugUIModule.cpp
        debug/DebugUIModule.h
        player/PlayerClientModule.cpp
        player/PlayerClientModule.h
        player/player_components.h
        world/planet/SurfaceWindowManager.cpp
        world/planet/SurfaceWindowManager.h
        world/planet/planet_client_components.h
        world/planet/PlanetClientModule.cpp
        world/planet/PlanetClientModule.h
)

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