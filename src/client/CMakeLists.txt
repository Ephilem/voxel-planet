add_library(VoxelPlanetClient STATIC
        ClientModule.cpp
        ClientModule.h
        debug/IDebugPanel.h
        debug/DebugUISystem.cpp
        debug/DebugUISystem.h
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