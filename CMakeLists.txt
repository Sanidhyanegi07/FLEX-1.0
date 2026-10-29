cmake_minimum_required(VERSION 3.20)
project(flex_native VERSION 1.0.0 LANGUAGES C CXX)

# Standard specifications
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configuration Options
option(FLEX_BUILD_DESKTOP "Build the native Qt 6 desktop application" ON)
option(FLEX_BUILD_TESTS "Build native test suites" ON)
option(FLEX_BUILD_BRIDGE "Build the C bridge CLI executable" ON)

# Discover Qt 6: honor CMAKE_PREFIX_PATH, Qt6_DIR, QTDIR, or standard local fallbacks
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT DEFINED Qt6_DIR)
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    elseif(EXISTS "C:/Qt/6.11.1/mingw_64")
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Sql Network Widgets Multimedia)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-O3>)
    add_compile_options(-ffunction-sections -fdata-sections)
    if(NOT APPLE AND NOT MSVC)
        add_link_options(-Wl,--gc-sections)
    endif()
endif()

# ------------------------------------------------------------------------------
# Core C Library (C11 bounded engine, validation, policy, DSP, stream parser)
# ------------------------------------------------------------------------------
add_library(flex_core_c STATIC
    core-c/src/command_parser.c
    core-c/src/state_manager.c
    core-c/src/orchestrator.c
    core-c/src/request_validator.c
    core-c/src/error_classifier.c
    core-c/src/session_state.c
    core-c/src/operation_policy.c
    core-c/src/tool_policy.c
    core-c/src/ai_stream_parser.c
    core-c/src/audio_dsp.c
    core-c/src/config_validator.c
    core-c/src/voice_fsm.c
    core-c/src/memory_index.c
    core-c/src/conversation_record.c
    core-c/src/workspace_analyzer.c
    core-c/src/telemetry_collector.c
    core-c/src/response_formatter.c
    core-c/src/ai_payload_builder.c
    core-c/src/theme_engine.c
    core-c/src/prompt_template.c
    core-c/src/whisper_pipeline.c
    core-c/src/tts_pipeline.c
    core-c/src/command_dispatcher.c
    core-c/src/context_compressor.c
    core-c/src/sqlite_codec.c
    core-c/src/markdown_parser.c
    core-c/src/text_tokenizer.c
    core-c/src/json_validator.c
    core-c/src/security_sandbox.c
    core-c/src/lexical_analyzer.c
    core-c/src/math_evaluator.c
    core-c/src/csv_table_formatter.c
    core-c/src/diff_engine.c
    core-c/src/cache_table.c
    core-c/src/sysinfo_collector.c
    core-c/src/color_palette.c
    core-c/src/unicode_support.c
    core-c/src/hash_table.c
    core-c/src/base64_codec.c
    core-c/src/url_parser.c
    core-c/src/regex_matcher.c
    core-c/src/crc32.c
    core-c/src/ansi_escape.c
    core-c/src/bloom_filter.c
    core-c/src/kv_store.c
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
# Voice Subsystem & Desktop UI Libraries (Stages 5 & 6B)
# ------------------------------------------------------------------------------
if(FLEX_BUILD_DESKTOP OR FLEX_BUILD_TESTS)
    # Third-Party Dependencies: whisper.cpp
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Do not build whisper tests" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Do not build whisper examples" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "Do not build whisper server" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
    add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

    # Native C++ Voice Subsystem Library
    add_library(flex_voice STATIC
        src-cpp/include/flex/voice/audio_buffer.hpp
        src-cpp/include/flex/voice/audio_capture_service.hpp
        src-cpp/include/flex/voice/text_to_speech_service.hpp
        src-cpp/include/flex/voice/speech_to_text_service.hpp
        src-cpp/include/flex/voice/whisper_model_resolver.hpp
        src-cpp/include/flex/voice/voice_conversation_controller.hpp
        src-cpp/include/flex/voice/tts_queue.hpp
        src-cpp/src/voice/audio_capture_service.cpp
        src-cpp/src/voice/text_to_speech_service.cpp
        src-cpp/src/voice/speech_to_text_service.cpp
        src-cpp/src/voice/whisper_model_resolver.cpp
        src-cpp/src/voice/voice_conversation_controller.cpp
    )

    set_target_properties(flex_voice PROPERTIES AUTOMOC ON)

    target_include_directories(flex_voice PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp/include
    )

    target_link_libraries(flex_voice PUBLIC
        flex_backend
        whisper
        Qt6::Core
        Qt6::Multimedia
        ole32
        sapi
    )

    # Native C++ Desktop GUI Library
    add_library(flex_ui STATIC
        src-cpp/include/flex/ui/theme.hpp
        src-cpp/include/flex/ui/ai_core_widget.hpp
        src-cpp/include/flex/ui/message_bubble.hpp
        src-cpp/include/flex/ui/conversation_list_widget.hpp
        src-cpp/include/flex/ui/chat_view.hpp
        src-cpp/include/flex/ui/top_header_widget.hpp
        src-cpp/include/flex/ui/navigation_sidebar.hpp
        src-cpp/include/flex/ui/right_sidebar.hpp
        src-cpp/include/flex/ui/prototype_view.hpp
        src-cpp/include/flex/ui/voice_studio_view.hpp
        src-cpp/include/flex/ui/memory_view.hpp
        src-cpp/include/flex/ui/projects_view.hpp
        src-cpp/include/flex/ui/settings_view.hpp
        src-cpp/include/flex/ui/main_window.hpp
        src-cpp/src/ui/theme.cpp
        src-cpp/src/ui/ai_core_widget.cpp
        src-cpp/src/ui/message_bubble.cpp
        src-cpp/src/ui/conversation_list_widget.cpp
        src-cpp/src/ui/chat_view.cpp
        src-cpp/src/ui/top_header_widget.cpp
        src-cpp/src/ui/navigation_sidebar.cpp
        src-cpp/src/ui/right_sidebar.cpp
        src-cpp/src/ui/prototype_view.cpp
        src-cpp/src/ui/voice_studio_view.cpp
        src-cpp/src/ui/memory_view.cpp
        src-cpp/src/ui/projects_view.cpp
        src-cpp/src/ui/settings_view.cpp
        src-cpp/src/ui/main_window.cpp
    )

    set_target_properties(flex_ui PROPERTIES AUTOMOC ON)

    target_include_directories(flex_ui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
    )

    target_link_libraries(flex_ui PUBLIC
        flex_backend
        flex_voice
        Qt6::Core
        Qt6::Sql
        Qt6::Network
        Qt6::Widgets
        Qt6::Multimedia
        ole32
        sapi
    )
endif()

# ------------------------------------------------------------------------------
# Native C++ Desktop GUI Executable (Stage 5)
# ------------------------------------------------------------------------------
if(FLEX_BUILD_DESKTOP)
    add_executable(flex_desktop
        src-cpp/src/main.cpp
    )

    target_link_libraries(flex_desktop PRIVATE
        flex_ui
        Qt6::Widgets
    )
endif()

# ------------------------------------------------------------------------------
# C Core Bridge Executable
# ------------------------------------------------------------------------------
if(FLEX_BUILD_BRIDGE)
    add_executable(flex_bridge
        core-c/src/bridge_main.c
    )

    target_link_libraries(flex_bridge PRIVATE
        flex_core_c
    )

    if(MINGW)
        target_link_options(flex_bridge PRIVATE -static)
    endif()
endif()

# ------------------------------------------------------------------------------
# Testing Targets
# ------------------------------------------------------------------------------
if(FLEX_BUILD_TESTS)
    enable_testing()

    # 1. C core test suite
    add_executable(flex_c_tests
        core-c/tests/test_command_parser.c
        core-c/tests/test_state_manager.c
        core-c/tests/test_orchestrator.c
        core-c/tests/test_c_core_modules.c
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

    # 7. Native C++ Voice tests (Stage 6B - Step 1, 2, 3)
    add_executable(flex_voice_tests
        src-cpp/tests/test_voice.cpp
    )

    set_target_properties(flex_voice_tests PROPERTIES AUTOMOC ON)

    target_include_directories(flex_voice_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src-cpp/include/flex
    )

    target_link_libraries(flex_voice_tests PRIVATE
        flex_voice
        flex_backend
        Qt6::Core
        Qt6::Multimedia
        Qt6::Sql
        Qt6::Network
        ole32
        sapi
    )

    add_test(NAME cpp_voice_tests COMMAND flex_voice_tests)
endif()
