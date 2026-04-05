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
        debug/world/WorldInfo.h
        debug/world/WorldInfo.cpp
        debug/performance/FpsCounter.h
        debug/performance/FpsCounter.cpp
        debug/renderer/VoxelBufferVisualizer.h
        debug/renderer/VoxelBufferVisualizer.cpp
        debug/entities/PlayerPanel.cpp
        debug/entities/PlayerPanel.h
        debug/world/ChunkManagerPanel.cpp
        debug/world/ChunkManagerPanel.h
        debug/world/WorldGenPanel.cpp
        debug/world/WorldGenPanel.h
        debug/DebugUIModule.cpp
        debug/DebugUIModule.h
        player/PlayerClientModule.cpp
        player/PlayerClientModule.h
        player/player_components.h
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