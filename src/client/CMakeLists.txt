file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(VoxelPlanetClient STATIC ${CLIENT_SOURCES})

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
