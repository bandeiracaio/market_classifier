cmake_minimum_required(VERSION 3.28)

add_executable(mc_tests
    test_decimal.cpp
    test_decimal_math.cpp
    test_domain_contracts.cpp
    test_bridge_protocol.cpp
    test_raw_frame.cpp
    test_json.cpp
    test_binance_adapter.cpp
    test_hyperliquid_adapter.cpp
    test_order_book.cpp
    test_binance_book_sync.cpp
    test_ring.cpp
    test_feed_state.cpp
    test_engine.cpp
    test_processors_trades.cpp
    test_processors_cvd.cpp
    test_processors_footprint.cpp
    test_processors_candles.cpp
    test_processors_heatmap.cpp
    test_processors_metrics.cpp
    test_replay_mvp.cpp
    test_panel_registry.cpp
    test_workspace_codec.cpp
    test_workspace_controller.cpp
    test_flood.cpp
    test_runtime.cpp
    test_replay.cpp
    test_version.cpp
)

target_include_directories(mc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(mc_tests PRIVATE
    Catch2::Catch2WithMain
    mc_domain
    mc_ui
)

target_compile_definitions(mc_tests PRIVATE MC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mc_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(mc_tests PRIVATE
        /W4 /WX /wd4100 /wd4267 /D_CRT_SECURE_NO_WARNINGS
    )
endif()

if(EMSCRIPTEN)
    # Node runner with direct host filesystem access so fixtures load from MC_SOURCE_DIR.
    target_link_options(mc_tests PRIVATE
        "SHELL:-s ENVIRONMENT=node"
        "SHELL:-s NODERAWFS=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s EXIT_RUNTIME=1"
        "SHELL:-s STACK_SIZE=1048576"
    )
    # The Emscripten toolchain sets node as CMAKE_CROSSCOMPILING_EMULATOR for targets.
    add_test(NAME mc_tests_wasm COMMAND mc_tests)
else()
    catch_discover_tests(mc_tests)
endif()
