cmake_minimum_required(VERSION 3.28)

# Emscripten does not ship clang-scan-deps. This project does not use C++20 modules,
# so dependency scanning is unnecessary and breaks Ninja builds on Windows.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(market_classifier
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy / IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# SDL2 — native builds only; Emscripten uses its built-in SDL2 port
# Pinned: release-2.30.8 (2024-09-26) https://github.com/libsdl-org/SDL/releases
# ADR-0001: FetchContent with pinned tag
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    # Silence SDL2's own warnings in project builds
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.8
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL2)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui — docking branch
# Pinned to the docking branch head verified on 2026-09-24.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        3bae66c735670619baf51391eba7f3d90a25d125
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui)

# Build Dear ImGui as a static library.
# The OpenGL3 backend supports both desktop OpenGL and OpenGL ES 3 / WebGL 2.
add_library(mc_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
)
# SYSTEM suppresses third-party warnings from our -Wall/-Werror umbrella
target_include_directories(mc_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
if(EMSCRIPTEN)
    # Use Emscripten's built-in SDL2 port; no FetchContent SDL2 needed
    target_compile_options(mc_imgui PUBLIC "SHELL:-s USE_SDL=2")
    target_link_options(mc_imgui PUBLIC "SHELL:-s USE_SDL=2")
else()
    # SDL_MAIN_HANDLED is defined in main.cpp so SDL2main is not needed.
    target_link_libraries(mc_imgui PUBLIC SDL2::SDL2-static)
endif()

# ---------------------------------------------------------------------------
# ImPlot — v0.17 (2023-11-08) https://github.com/epezent/implot/releases
# ADR-0001: FetchContent with pinned tag
# ---------------------------------------------------------------------------
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(implot)

add_library(mc_implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(mc_implot SYSTEM PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(mc_implot PUBLIC mc_imgui)

# ---------------------------------------------------------------------------
# yyjson — JSON parser for venue adapters (ADR-0005)
# Pinned: 0.10.0 https://github.com/ibireme/yyjson/releases
# Built as our own static C library; upstream CMake is not added (SOURCE_SUBDIR
# points at a non-existent dir) so only yyjson.c is compiled.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.10.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  mc-no-cmake
)
FetchContent_MakeAvailable(yyjson)
add_library(mc_yyjson STATIC ${yyjson_SOURCE_DIR}/src/yyjson.c)
set_target_properties(mc_yyjson PROPERTIES LINKER_LANGUAGE C)
target_include_directories(mc_yyjson SYSTEM PUBLIC ${yyjson_SOURCE_DIR}/src)
# Reader only: the engine never serializes through yyjson.
target_compile_definitions(mc_yyjson PRIVATE YYJSON_DISABLE_WRITER=1 YYJSON_DISABLE_UTILS=1)

# ---------------------------------------------------------------------------
# Catch2 — test framework (ADR-0002). Native by default; MC_WASM_TESTS=ON also builds the
# suite with Emscripten for Node so replay tests run in WASM (packet §12 criterion 1).
# Pinned: v3.7.1 (2024-10-04) https://github.com/catchorg/Catch2/releases
# ---------------------------------------------------------------------------
option(MC_WASM_TESTS "Build the Catch2 suite for Node under Emscripten" OFF)
if(NOT EMSCRIPTEN OR MC_WASM_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()

add_subdirectory(engine)
