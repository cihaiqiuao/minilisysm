cmake_minimum_required(VERSION 3.16)
project(minilisysm VERSION 0.1.0 LANGUAGES CXX)

option(MINILISYSM_BUILD_TESTS "Build unit tests" ON)
option(MINILISYSM_BUILD_TOOLS "Build benchmark tools" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minilisysm_core
    src/core/config.cpp
    src/core/time.cpp
    src/collectors/collector_factory.cpp
    src/collectors/linux_proc_reader.cpp
    src/collectors/meminfo_collector.cpp
    src/collectors/sched_delay_collector.cpp
    src/collectors/self_status_collector.cpp
    src/rules/rule_factory.cpp
    src/rules/rule_engine.cpp
    src/runtime/monitor.cpp
    src/runtime/thread_policy.cpp
    src/storage/event_serializer.cpp
    src/storage/event_store.cpp
    src/storage/storage_factory.cpp
)

target_include_directories(minilisysm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minilisysm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(minilisysm src/main.cpp)
target_link_libraries(minilisysm PRIVATE minilisysm_core)

if(MINILISYSM_BUILD_TESTS)
    enable_testing()
    add_executable(test_spsc tests/test_spsc.cpp)
    target_link_libraries(test_spsc PRIVATE minilisysm_core)
    add_test(NAME spsc_queue COMMAND test_spsc)

    add_executable(test_rule_engine tests/test_rule_engine.cpp)
    target_link_libraries(test_rule_engine PRIVATE minilisysm_core)
    add_test(NAME rule_engine COMMAND test_rule_engine)

    add_executable(test_factories tests/test_factories.cpp)
    target_link_libraries(test_factories PRIVATE minilisysm_core)
    add_test(NAME factories COMMAND test_factories)

    add_executable(test_event_serializer tests/test_event_serializer.cpp)
    target_link_libraries(test_event_serializer PRIVATE minilisysm_core)
    add_test(NAME event_serializer COMMAND test_event_serializer)
endif()

if(MINILISYSM_BUILD_TOOLS)
    add_executable(bench_spsc tools/bench_spsc.cpp)
    target_link_libraries(bench_spsc PRIVATE minilisysm_core)
endif()
