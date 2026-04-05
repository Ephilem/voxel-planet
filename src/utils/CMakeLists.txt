add_library(VoxelPlanetUtils INTERFACE
        common/BaseModule.h)

add_library(VoxelPlanet::Utils ALIAS VoxelPlanetUtils)

target_include_directories(VoxelPlanetUtils
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)