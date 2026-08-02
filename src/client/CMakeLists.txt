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
        debug/world/PlanetLayerViewer.h
        debug/world/PlanetLayerViewer.cpp
        debug/world/PlanetLodPanel.h
        debug/world/PlanetLodPanel.cpp
        debug/world/PlanetDirectionField.h
        debug/performance/FpsCounter.h
        debug/performance/FpsCounter.cpp
        debug/entities/PlayerPanel.cpp
        debug/entities/PlayerPanel.h
        debug/DebugUIModule.cpp
        debug/DebugUIModule.h
        player/PlayerClientModule.cpp
        player/PlayerClientModule.h
        player/player_components.h
        world/planet/planet_client_components.h
        world/planet/PlanetClientModule.cpp
        world/planet/PlanetClientModule.h
        debug/world/PlanetDebugLayer.cpp
        debug/world/PlanetDebugLayer.h
        debug/world/PlanetLayerProjectionMappings.cpp
        debug/world/PlanetLayerProjectionMappings.h
        world/planet/PlanetQuadtrees.h
        world/planet/PlanetQuadtrees.cpp
)

add_library(VoxelPlanet::Client ALIAS VoxelPlanetClient)

target_include_directories(VoxelPlanetClient
        PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

# std::async / std::future : sans pthread, GCC + glibc lancent un
# std::system_error a l'execution au lieu de demarrer le thread.
find_package(Threads REQUIRED)

target_link_libraries(VoxelPlanetClient
        PUBLIC
        glm::glm
        flecs::flecs_static
        concurrentqueue
        PRIVATE
        implot
        Threads::Threads
        VoxelPlanet::Core
        VoxelPlanet::Platform
        VoxelPlanet::Renderer
)