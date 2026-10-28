cmake_minimum_required(VERSION 3.20)
project(flex_native VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
    if(MINGW)
        add_link_options(-static)
    endif()
endif()

# ------------------------------------------------------------------------------
# Core C Library
# ------------------------------------------------------------------------------
add_library(flex_core_c STATIC
    core-c/src/command_parser.c
    core-c/src/state_manager.c
    core-c/src/orchestrator.c
)

target_include_directories(flex_core_c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core-c/include
)

# ------------------------------------------------------------------------------
# C++ Core Wrapper Library
# ------------------------------------------------------------------------------
add_library(flex_core_cpp STATIC
    src-cpp/src/core_session.cpp
)

target_include_directories(flex_core_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
)

target_link_libraries(flex_core_cpp PUBLIC
    flex_core_c
)

# ------------------------------------------------------------------------------
# Existing C Bridge Executable
# ------------------------------------------------------------------------------
add_executable(flex_bridge
    core-c/src/bridge_main.c
)

target_link_libraries(flex_bridge PRIVATE
    flex_core_c
)

# ------------------------------------------------------------------------------
# Testing Targets
# ------------------------------------------------------------------------------
enable_testing()

# Existing C core test suite
add_executable(flex_c_tests
    core-c/tests/test_command_parser.c
    core-c/tests/test_state_manager.c
    core-c/tests/test_orchestrator.c
)

target_include_directories(flex_c_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core-c/tests
)

target_link_libraries(flex_c_tests PRIVATE
    flex_core_c
)

add_test(NAME c_core_tests COMMAND flex_c_tests)

# Native C++ wrapper test suite
add_executable(flex_cpp_tests
    src-cpp/tests/test_flex_wrapper.cpp
)

target_link_libraries(flex_cpp_tests PRIVATE
    flex_core_cpp
)

add_test(NAME cpp_wrapper_tests COMMAND flex_cpp_tests)
