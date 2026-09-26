cmake_minimum_required(VERSION 3.28)

add_executable(mc_tests
    test_version.cpp
)

target_include_directories(mc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(mc_tests PRIVATE
    Catch2::Catch2WithMain
)

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
