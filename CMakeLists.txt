cmake_minimum_required(VERSION 3.25)
project(VoxelPlanet VERSION 0.1.0 LANGUAGES CXX)

set_property(GLOBAL APPEND PROPERTY JOB_POOLS compile_pool=6 link_pool=1)
set(CMAKE_JOB_POOL_COMPILE compile_pool)
set(CMAKE_JOB_POOL_LINK link_pool)

######
###### Versioning: MAJOR.MINOR.PATCH+build.NUMBER.HASH
######

execute_process(
    COMMAND git rev-parse --short=8 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

execute_process(
    COMMAND git diff --quiet HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_DIRTY
)

if(NOT DEFINED BUILD_NUMBER)
    set(BUILD_NUMBER "0")
endif()

if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()

if(NOT GIT_BRANCH)
    set(GIT_BRANCH "unknown")
endif()

if(GIT_DIRTY)
    set(GIT_DIRTY_SUFFIX "-dirty")
else()
    set(GIT_DIRTY_SUFFIX "")
endif()

set(VOXELPLANET_VERSION_FULL "${PROJECT_VERSION}+build.${BUILD_NUMBER}.${GIT_COMMIT_HASH}${GIT_DIRTY_SUFFIX}")

set(VOXELPLANET_VERSION_SHORT "${PROJECT_VERSION}-${GIT_COMMIT_HASH}${GIT_DIRTY_SUFFIX}")

message(STATUS "VoxelPlanet Version: ${VOXELPLANET_VERSION_FULL}")
message(STATUS "Git Branch: ${GIT_BRANCH}")

add_compile_definitions(VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

# Windows-specific: prevent min/max macros and reduce Windows.h bloat
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(ENABLE_ASAN "Enable Address Sanitizer (Debug)" OFF)
option(ENABLE_IWYU "Enable include-what-you-use" OFF)
# libc++ only: strip the STL's transitive includes on our targets (not the dependencies), so a
# missing <span>/<vector>/... fails here instead of on MSVC. Used by the linux-strict preset.
option(ENABLE_STRICT_INCLUDES "Remove libc++ transitive includes on project targets" OFF)

if(ENABLE_IWYU)
    find_program(IWYU_PATH NAMES include-what-you-use iwyu REQUIRED)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_PATH};-Xiwyu;--no_fwd_decls")
endif()


set(ORIGINAL_FLAGS ${CMAKE_CXX_FLAGS})

if(ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
    endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)

set(CMAKE_CXX_FLAGS ${ORIGINAL_FLAGS})

add_subdirectory(src/utils)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/renderer)
add_subdirectory(src/client)

# Link Tracy to renderer if enabled
if(TRACY_FOUND)
    target_link_libraries(VoxelPlanetRenderer PUBLIC TracyClient)
    target_compile_definitions(VoxelPlanetRenderer PUBLIC TRACY_ENABLE)

    target_link_libraries(VoxelPlanetCore PUBLIC TracyClient)
    target_compile_definitions(VoxelPlanetCore PUBLIC TRACY_ENABLE)
endif()

add_executable(${PROJECT_NAME}
        src/main.cpp
        src/vulkan_dispatcher.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
        VoxelPlanet::Renderer
        VoxelPlanet::Platform
        VoxelPlanet::Core
        VoxelPlanet::Client
        nvrhi_vk
        nvrhi
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        glfw
        glm::glm
        flecs::flecs_static
        imgui
        implot
        FastNoise2
        stb
)

if(TRACY_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TRACY_ENABLE)
endif()

# Version definitions accessible in C++ code
target_compile_definitions(${PROJECT_NAME} PRIVATE
        VOXELPLANET_VERSION="${PROJECT_VERSION}"
        VOXELPLANET_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        VOXELPLANET_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        VOXELPLANET_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VOXELPLANET_VERSION_FULL="${VOXELPLANET_VERSION_FULL}"
        VOXELPLANET_VERSION_SHORT="${VOXELPLANET_VERSION_SHORT}"
        VOXELPLANET_BUILD_NUMBER=${BUILD_NUMBER}
        VOXELPLANET_GIT_HASH="${GIT_COMMIT_HASH}"
        VOXELPLANET_GIT_BRANCH="${GIT_BRANCH}"
)

if(ENABLE_STRICT_INCLUDES)
    foreach(_t VoxelPlanetCore VoxelPlanetPlatform VoxelPlanetRenderer VoxelPlanetClient ${PROJECT_NAME})
        target_compile_definitions(${_t} PRIVATE _LIBCPP_REMOVE_TRANSITIVE_INCLUDES)
    endforeach()
endif()

file(GLOB_RECURSE SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/shaders/*.vert"
        "${CMAKE_SOURCE_DIR}/assets/shaders/*.frag"
        "${CMAKE_SOURCE_DIR}/assets/shaders/*.comp"
)

# Shared headers pulled in with #include. glslc does not emit a depfile here, so every shader is
# made to depend on all of them: touching planet_lod_node.glsl has to rebuild the traversal, or
# the SPIR-V silently keeps the previous definitions.
file(GLOB_RECURSE SHADER_INCLUDES
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/shaders/*.glsl"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMENT "Copying assets to build directory"
)


foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    get_filename_component(SHADER_EXT ${SHADER} LAST_EXT)
    set(SHADER_OUTPUT "${CMAKE_BINARY_DIR}/assets/shaders/${SHADER_NAME}.spv")

    if(SHADER_EXT STREQUAL ".vert")
        set(SHADER_STAGE "vert")
    elseif(SHADER_EXT STREQUAL ".frag")
        set(SHADER_STAGE "frag")
    elseif(SHADER_EXT STREQUAL ".comp")
        set(SHADER_STAGE "comp")
    else()
        message(FATAL_ERROR "Unknown shader extension: ${SHADER_EXT}")
    endif()

    add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND Vulkan::glslc
                --target-env=vulkan1.1
                --target-spv=spv1.3
                -fshader-stage=${SHADER_STAGE}
                -g
                ${SHADER} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER} ${SHADER_INCLUDES}
            COMMENT "Compiling shader ${SHADER_NAME}"
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} shaders)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets
        DESTINATION bin
)
