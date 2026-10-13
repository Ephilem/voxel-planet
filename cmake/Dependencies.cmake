######
###### Dependencies
######

include(FetchContent)

function(optimize_external_targets dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach (_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        get_target_property(_imported ${_t} IMPORTED)

        if (NOT _imported AND _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|MODULE_LIBRARY|EXECUTABLE)$")
            target_compile_options(${_t} PRIVATE
                    $<$<CONFIG:Debug>:$<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>>
            )
        endif ()
    endforeach ()

    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach (_s IN LISTS _subs)
        optimize_external_targets("${_s}")
    endforeach ()
endfunction()


# Optional: Tracy profiler
option(ENABLE_TRACY "Enable Tracy profiler" ON)

if (ENABLE_TRACY)
    FetchContent_Declare(
            tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy.git
            GIT_TAG v0.14.1
            GIT_SHALLOW TRUE
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_VERBOSE OFF CACHE BOOL "" FORCE)
    set(TRACY_TIMER_FALLBACK ON CACHE BOOL "" FORCE)
    set(TRACY_DISALLOW_HW_TIMER ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    set(TRACY_FOUND TRUE)
    message(STATUS "Tracy profiler enabled")
else ()
    set(TRACY_FOUND FALSE)
    message(STATUS "Tracy profiler disabled")
endif ()


find_package(Vulkan REQUIRED)

FetchContent_Declare(
        vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
        GIT_TAG v1.3.290
)
FetchContent_MakeAvailable(vk-bootstrap)

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
        GIT_TAG v4.1.6
)
FetchContent_MakeAvailable(flecs)

if (ENABLE_TRACY)
    target_compile_definitions(flecs_static PUBLIC FLECS_PERF_TRACE)
endif ()

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
        GIT_TAG 12.2.0
)
FetchContent_MakeAvailable(fmt)

# moodycamel concurrentqueue
FetchContent_Declare(
        concurrentqueue
        GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
        GIT_TAG v1.0.5
)
FetchContent_MakeAvailable(concurrentqueue)

foreach (_dep tracy vk-bootstrap glfw nvrhi fastnoise2 fmt)
    string(TOLOWER "${_dep}" _lc)
    if (DEFINED ${_lc}_SOURCE_DIR)
        optimize_external_targets("${${_lc}_SOURCE_DIR}")
    endif ()
endforeach ()

####
#### End Dependencies
####
