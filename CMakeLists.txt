cmake_minimum_required(VERSION 3.25)
project(VoxelPlanet VERSION 0.1.0 LANGUAGES CXX)

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

######
###### Dependencies
######

include(FetchContent)

function(optimize_external_targets dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        get_target_property(_imported ${_t} IMPORTED)

        if(NOT _imported AND _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|MODULE_LIBRARY|EXECUTABLE)$")
            target_compile_options(${_t} PRIVATE
                    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>>
            )
        endif()
    endforeach()

    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subs)
        optimize_external_targets("${_s}")
    endforeach()
endfunction()


# Optional: Tracy profiler
option(ENABLE_TRACY "Enable Tracy profiler" ON)

if(ENABLE_TRACY)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.13.1
        GIT_SHALLOW TRUE
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    set(TRACY_FOUND TRUE)
    message(STATUS "Tracy profiler enabled")
else()
    set(TRACY_FOUND FALSE)
    message(STATUS "Tracy profiler disabled")
endif()


find_package(Vulkan REQUIRED)

FetchContent_Declare(
        vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
        GIT_TAG v1.3.290
)
FetchContent_MakeAvailable(vk-bootstrap)

FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        GIT_TAG v3.1.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
        flecs
        GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
        GIT_TAG v4.0.3
)
FetchContent_MakeAvailable(flecs)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v0.17
)
FetchContent_MakeAvailable(implot)

add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

FetchContent_Declare(
        nvrhi
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
        GIT_TAG 2ac4b58c355f53827c2e1d0740849ce4d09d5dd6
)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nvrhi)

FetchContent_Declare(
        fastnoise2
        GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
        GIT_TAG v0.10.0-alpha
)
set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fastnoise2)

# stb (images)
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# fmt (formatting)
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.2
)
FetchContent_MakeAvailable(fmt)

# moodycamel concurrentqueue
FetchContent_Declare(
        concurrentqueue
        GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
        GIT_TAG v1.0.5
)
FetchContent_MakeAvailable(concurrentqueue)

set(CMAKE_CXX_FLAGS ${ORIGINAL_FLAGS})

foreach(_dep tracy vk-bootstrap VulkanMemoryAllocator glfw flecs nvrhi fastnoise2 fmt)
    string(TOLOWER "${_dep}" _lc)
    if(DEFINED ${_lc}_SOURCE_DIR)
        optimize_external_targets("${${_lc}_SOURCE_DIR}")
    endif()
endforeach()

####
#### End Dependencies
####

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
        VulkanMemoryAllocator
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
