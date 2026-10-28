cmake_minimum_required(VERSION 3.20)
project(flex_native VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add Qt 6.11.1 MinGW-64 installation path to prefix if not set
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Widgets)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
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
# C++ Core Wrapper Library (Stage 1)
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
# Native C++ SQLite Storage Library (Stage 2)
# ------------------------------------------------------------------------------
add_library(flex_storage STATIC
    src-cpp/src/storage.cpp
)

target_include_directories(flex_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
)

target_link_libraries(flex_storage PUBLIC
    Qt6::Core
    Qt6::Sql
)

# ------------------------------------------------------------------------------
# Native C++ Ollama AI Client Library (Stage 3)
# ------------------------------------------------------------------------------
add_library(flex_ai STATIC
    src-cpp/include/flex/ollama_client.hpp
    src-cpp/src/ollama_client.cpp
)

set_target_properties(flex_ai PROPERTIES AUTOMOC ON)

target_include_directories(flex_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
)

target_link_libraries(flex_ai PUBLIC
    Qt6::Core
    Qt6::Network
)

# ------------------------------------------------------------------------------
# Native C++ Backend Coordinator Library (Stage 4)
# ------------------------------------------------------------------------------
add_library(flex_backend STATIC
    src-cpp/include/flex/backend.hpp
    src-cpp/src/backend.cpp
)

set_target_properties(flex_backend PROPERTIES AUTOMOC ON)

target_include_directories(flex_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
)

target_link_libraries(flex_backend PUBLIC
    flex_core_cpp
    flex_storage
    flex_ai
    Qt6::Core
    Qt6::Sql
    Qt6::Network
)

# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
# Native C++ Desktop GUI Library (Stage 5)
# ------------------------------------------------------------------------------
add_library(flex_ui STATIC
    src-cpp/include/flex/ui/theme.hpp
    src-cpp/include/flex/ui/ai_core_widget.hpp
    src-cpp/include/flex/ui/message_bubble.hpp
    src-cpp/include/flex/ui/conversation_list_widget.hpp
    src-cpp/include/flex/ui/chat_view.hpp
    src-cpp/include/flex/ui/main_window.hpp
    src-cpp/src/ui/theme.cpp
    src-cpp/src/ui/ai_core_widget.cpp
    src-cpp/src/ui/message_bubble.cpp
    src-cpp/src/ui/conversation_list_widget.cpp
    src-cpp/src/ui/chat_view.cpp
    src-cpp/src/ui/main_window.cpp
)

set_target_properties(flex_ui PROPERTIES AUTOMOC ON)

target_include_directories(flex_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
)

target_link_libraries(flex_ui PUBLIC
    flex_backend
    Qt6::Core
    Qt6::Sql
    Qt6::Network
    Qt6::Widgets
)

# ------------------------------------------------------------------------------
# Native C++ Desktop GUI Executable (Stage 5)
# ------------------------------------------------------------------------------
add_executable(flex_desktop
    src-cpp/src/main.cpp
)

target_link_libraries(flex_desktop PRIVATE
    flex_ui
    Qt6::Widgets
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

if(MINGW)
    target_link_options(flex_bridge PRIVATE -static)
endif()

# ------------------------------------------------------------------------------
# Testing Targets
# ------------------------------------------------------------------------------
enable_testing()

# 1. Existing C core test suite
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

if(MINGW)
    target_link_options(flex_c_tests PRIVATE -static)
endif()

add_test(NAME c_core_tests COMMAND flex_c_tests)

# 2. Native C++ wrapper test suite (Stage 1)
add_executable(flex_cpp_tests
    src-cpp/tests/test_flex_wrapper.cpp
)

target_link_libraries(flex_cpp_tests PRIVATE
    flex_core_cpp
)

if(MINGW)
    target_link_options(flex_cpp_tests PRIVATE -static)
endif()

add_test(NAME cpp_wrapper_tests COMMAND flex_cpp_tests)

# 3. Native C++ SQLite storage test suite (Stage 2)
add_executable(flex_storage_tests
    src-cpp/tests/test_storage.cpp
)

target_link_libraries(flex_storage_tests PRIVATE
    flex_storage
    Qt6::Core
    Qt6::Sql
)

add_test(NAME cpp_storage_tests COMMAND flex_storage_tests)

# 4. Native C++ Ollama client test suite (Stage 3)
add_executable(flex_ollama_tests
    src-cpp/tests/test_ollama_client.cpp
)

set_target_properties(flex_ollama_tests PROPERTIES AUTOMOC ON)

target_link_libraries(flex_ollama_tests PRIVATE
    flex_ai
    Qt6::Core
    Qt6::Network
)

add_test(NAME cpp_ollama_tests COMMAND flex_ollama_tests)

# 5. Native C++ backend integration test suite (Stage 4)
add_executable(flex_backend_tests
    src-cpp/tests/test_backend.cpp
)

set_target_properties(flex_backend_tests PROPERTIES AUTOMOC ON)

target_include_directories(flex_backend_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
)

target_link_libraries(flex_backend_tests PRIVATE
    flex_backend
    Qt6::Core
    Qt6::Sql
    Qt6::Network
)

add_test(NAME cpp_backend_tests COMMAND flex_backend_tests)

# 6. Native C++ Qt UI test suite (Stage 5)
add_executable(flex_ui_tests
    src-cpp/tests/test_ui.cpp
)

set_target_properties(flex_ui_tests PROPERTIES AUTOMOC ON)

target_include_directories(flex_ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
)

target_link_libraries(flex_ui_tests PRIVATE
    flex_ui
    Qt6::Core
    Qt6::Sql
    Qt6::Network
    Qt6::Widgets
)

add_test(NAME cpp_ui_tests COMMAND flex_ui_tests)
