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

catch_discover_tests(mc_tests)
