cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# Pure domain library — no UI, platform, Emscripten, or venue dependencies
# ---------------------------------------------------------------------------
add_library(mc_domain STATIC
    src/bridge/protocol.cpp
    src/domain/decimal.cpp
    src/domain/types.cpp
    src/runtime/clock.cpp
    src/runtime/ingress.cpp
)

target_include_directories(mc_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mc_domain PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(mc_domain PRIVATE
        /W4 /WX /wd4100 /wd4267 /D_CRT_SECURE_NO_WARNINGS
    )
endif()

# ---------------------------------------------------------------------------
# Terminal executable / WASM module
# ---------------------------------------------------------------------------
add_executable(market_classifier
    src/main.cpp
)

target_include_directories(market_classifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(market_classifier PRIVATE
    mc_domain
    mc_imgui
    mc_implot
)

# The application render loop calls desktop OpenGL entry points directly.
# SDL creates the context but does not provide the OpenGL import library.
if(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    target_link_libraries(market_classifier PRIVATE OpenGL::GL)
endif()

# Treat warnings as errors for project code only (third-party excluded via SYSTEM includes)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(market_classifier PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(market_classifier PRIVATE
        /W4 /WX /wd4100 /wd4267 /D_CRT_SECURE_NO_WARNINGS
    )
endif()

if(EMSCRIPTEN)
    # Output .js + .wasm pair in the SvelteKit static directory.
    set(WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/apps/web/static/wasm")
    file(MAKE_DIRECTORY "${WASM_OUTPUT_DIR}")

    set_target_properties(market_classifier PROPERTIES
        OUTPUT_NAME    "market_classifier"
        SUFFIX         ".js"
        RUNTIME_OUTPUT_DIRECTORY "${WASM_OUTPUT_DIR}"
    )

    # WebGL 2 / OpenGL ES 3 + SDL2 port + single-threaded (ADR: no pthreads for M0).
    # ALLOW_MEMORY_GROWTH lets the heap expand under demo load without a hard static cap.
    # ENVIRONMENT=web prevents Node.js/worker environment shimming in the output bundle.
    # MODULARIZE + EXPORT_NAME produce a factory function referenced by terminal.ts.
    # main() is called automatically on module initialization (no --no-entry).
    target_link_options(market_classifier PRIVATE
        "SHELL:-s USE_SDL=2"
        "SHELL:-s FULL_ES3=1"
        "SHELL:-s MAX_WEBGL_VERSION=2"
        "SHELL:-s MIN_WEBGL_VERSION=2"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s ENVIRONMENT=web"
        "SHELL:-s EXPORT_NAME=MarketClassifier"
        "SHELL:-s MODULARIZE=1"
    )

    # Copy .wasm alongside .js — Emscripten generates both with matching names
    add_custom_command(TARGET market_classifier POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
            "WASM build complete: ${WASM_OUTPUT_DIR}/market_classifier.js + .wasm"
    )
endif()

# ---------------------------------------------------------------------------
# Native unit tests — Catch2, not built for WASM
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    add_subdirectory(tests)
endif()
