cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# Pure domain library — no UI, platform, Emscripten, or venue dependencies
# ---------------------------------------------------------------------------
add_library(mc_domain STATIC
    src/bridge/browser_api.cpp
    src/bridge/protocol.cpp
    src/bridge/raw_frame.cpp
    src/books/binance_book_sync.cpp
    src/books/order_book.cpp
    src/domain/decimal.cpp
    src/domain/decimal_math.cpp
    src/domain/types.cpp
    src/json/json.cpp
    src/processors/candles.cpp
    src/processors/cvd.cpp
    src/processors/footprint.cpp
    src/processors/heatmap.cpp
    src/processors/metrics.cpp
    src/processors/trades.cpp
    src/venues/binance_adapter.cpp
    src/venues/hyperliquid_adapter.cpp
    src/venues/instruments.cpp
    src/runtime/clock.cpp
    src/runtime/engine.cpp
    src/runtime/feed_state.cpp
    src/runtime/ingress.cpp
    src/runtime/read_model.cpp
    src/runtime/replay.cpp
)

target_include_directories(mc_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(mc_domain PRIVATE mc_yyjson)

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
# UI library — panels and workspace; reads engine read models only
# ---------------------------------------------------------------------------
add_library(mc_ui STATIC
    src/ui/panel_host.cpp
    src/ui/panel_registry.cpp
    src/ui/panels/bbo_spread.cpp
    src/ui/panels/candles.cpp
    src/ui/panels/cvd.cpp
    src/ui/panels/depth.cpp
    src/ui/panels/diagnostics.cpp
    src/ui/panels/footprint.cpp
    src/ui/panels/funding_oi.cpp
    src/ui/panels/heatmap.cpp
    src/ui/panels/ladder.cpp
    src/ui/panels/liquidations.cpp
    src/ui/panels/overview.cpp
    src/ui/panels/trades_tape.cpp
    src/ui/panels/volume_profile.cpp
    src/ui/workspace.cpp
    src/ui/workspace_codec.cpp
    src/ui/workspace_controller.cpp
)
target_include_directories(mc_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mc_ui PUBLIC mc_domain mc_imgui mc_implot)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mc_ui PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(mc_ui PRIVATE /W4 /WX /wd4100 /wd4267 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Terminal executable / WASM module
# ---------------------------------------------------------------------------
add_executable(market_classifier
    src/main.cpp
    src/app/app.cpp
)

target_include_directories(market_classifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(market_classifier PRIVATE
    mc_domain
    mc_ui
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
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['HEAPU8','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_mc_bridge_decode','_mc_bridge_result_name','_mc_submit_raw','_mc_socket_event','_mc_should_reconnect','_mc_request_snapshot','_mc_metadata_failed','_mc_retry_venue','_mc_engine_frame','_mc_venue_stat','_mc_debug_open_panel','_mc_debug_activate_layout','_mc_debug_save_layout_as','_mc_workspace_export','_mc_workspace_import','_mc_workspace_reset','_mc_workspace_dirty','_malloc','_free']"
    )

    # Copy .wasm alongside .js — Emscripten generates both with matching names
    add_custom_command(TARGET market_classifier POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
            "WASM build complete: ${WASM_OUTPUT_DIR}/market_classifier.js + .wasm"
    )
endif()

# ---------------------------------------------------------------------------
# Unit tests — Catch2. Native always; WASM (Node) when MC_WASM_TESTS=ON.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN OR MC_WASM_TESTS)
    add_subdirectory(tests)
endif()
