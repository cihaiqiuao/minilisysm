cmake_minimum_required(VERSION 3.16)
project(minilisysm VERSION 0.1.0 LANGUAGES CXX)

include(cmake/MinilisysmOptions.cmake)
include(cmake/MinilisysmCompilerWarnings.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minilisysm_core
    src/core/config.cpp
    src/core/time.cpp
    src/collectors/collector_factory.cpp
    src/collectors/io_delay_collector.cpp
    src/collectors/linux_proc_reader.cpp
    src/collectors/meminfo_collector.cpp
    src/collectors/sched_delay_collector.cpp
    src/collectors/self_status_collector.cpp
    src/rules/rule_factory.cpp
    src/rules/rule_engine.cpp
    src/runtime/event_dispatcher.cpp
    src/runtime/metric_registry.cpp
    src/runtime/metrics_server.cpp
    src/runtime/monitor.cpp
    src/runtime/thread_policy.cpp
    src/storage/event_serializer.cpp
    src/storage/jsonl_event_sink.cpp
    src/storage/network_event_sink.cpp
    src/storage/storage_factory.cpp
)

target_include_directories(minilisysm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MINILISYSM_ENABLE_EBPF)
    find_program(CLANG_EXECUTABLE clang REQUIRED)
    find_program(BPFTOOL_EXECUTABLE bpftool REQUIRED)
    find_program(LLVM_STRIP_EXECUTABLE llvm-strip)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBBPF REQUIRED IMPORTED_TARGET libbpf)

    set(MINILISYSM_BPF_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/ebpf)
    set(MINILISYSM_BPF_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/ebpf/bpf/sched_delay.bpf.c)
    set(MINILISYSM_VMLINUX_H ${MINILISYSM_BPF_GENERATED_DIR}/vmlinux.h)
    set(MINILISYSM_BPF_OBJECT ${MINILISYSM_BPF_GENERATED_DIR}/sched_delay.bpf.o)
    set(MINILISYSM_BPF_SKEL ${MINILISYSM_BPF_GENERATED_DIR}/sched_delay.skel.h)
    math(EXPR MINILISYSM_EBPF_RINGBUF_BYTES "${MINILISYSM_EBPF_RINGBUF_KB} * 1024")

    if(NOT EXISTS /sys/kernel/btf/vmlinux)
        message(FATAL_ERROR "MINILISYSM_ENABLE_EBPF requires /sys/kernel/btf/vmlinux for CO-RE skeleton generation")
    endif()
    execute_process(
        COMMAND ${BPFTOOL_EXECUTABLE} version
        RESULT_VARIABLE MINILISYSM_BPFTOOL_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT MINILISYSM_BPFTOOL_RESULT EQUAL 0)
        message(FATAL_ERROR "bpftool was found at ${BPFTOOL_EXECUTABLE}, but it is not runnable. Set BPFTOOL=/path/to/a/working/bpftool or install a linux-tools package that provides one.")
    endif()

    add_custom_command(
        OUTPUT ${MINILISYSM_VMLINUX_H}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MINILISYSM_BPF_GENERATED_DIR}
        COMMAND sh -c "${BPFTOOL_EXECUTABLE} btf dump file /sys/kernel/btf/vmlinux format c > '${MINILISYSM_VMLINUX_H}'"
        VERBATIM
    )

    if(LLVM_STRIP_EXECUTABLE)
        add_custom_command(
            OUTPUT ${MINILISYSM_BPF_OBJECT}
            DEPENDS ${MINILISYSM_BPF_SOURCE} ${MINILISYSM_VMLINUX_H}
            COMMAND ${CLANG_EXECUTABLE}
                -g -O2 -target bpf -D__TARGET_ARCH_x86
                -DMINILISYSM_EBPF_RINGBUF_BYTES=${MINILISYSM_EBPF_RINGBUF_BYTES}
                -DMINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES=${MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES}
                -I${MINILISYSM_BPF_GENERATED_DIR}
                -c ${MINILISYSM_BPF_SOURCE}
                -o ${MINILISYSM_BPF_OBJECT}
            COMMAND ${LLVM_STRIP_EXECUTABLE} -g ${MINILISYSM_BPF_OBJECT}
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${MINILISYSM_BPF_OBJECT}
            DEPENDS ${MINILISYSM_BPF_SOURCE} ${MINILISYSM_VMLINUX_H}
            COMMAND ${CLANG_EXECUTABLE}
                -g -O2 -target bpf -D__TARGET_ARCH_x86
                -DMINILISYSM_EBPF_RINGBUF_BYTES=${MINILISYSM_EBPF_RINGBUF_BYTES}
                -DMINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES=${MINILISYSM_EBPF_AGGREGATE_MAX_ENTRIES}
                -I${MINILISYSM_BPF_GENERATED_DIR}
                -c ${MINILISYSM_BPF_SOURCE}
                -o ${MINILISYSM_BPF_OBJECT}
            VERBATIM
        )
    endif()

    add_custom_command(
        OUTPUT ${MINILISYSM_BPF_SKEL}
        DEPENDS ${MINILISYSM_BPF_OBJECT}
        COMMAND sh -c "${BPFTOOL_EXECUTABLE} gen skeleton '${MINILISYSM_BPF_OBJECT}' > '${MINILISYSM_BPF_SKEL}'"
        VERBATIM
    )

    add_custom_target(minilisysm_bpf_skeleton DEPENDS ${MINILISYSM_BPF_SKEL})

    target_sources(minilisysm_core PRIVATE
        ebpf/src/ebpf_sched_delay_collector.cpp
    )
    add_dependencies(minilisysm_core minilisysm_bpf_skeleton)
    target_include_directories(minilisysm_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ebpf/include
        ${MINILISYSM_BPF_GENERATED_DIR}
    )
    target_link_libraries(minilisysm_core PUBLIC PkgConfig::LIBBPF)
    target_compile_definitions(minilisysm_core PUBLIC MINILISYSM_ENABLE_EBPF=1)
else()
    target_compile_definitions(minilisysm_core PUBLIC MINILISYSM_ENABLE_EBPF=0)
endif()

minilisysm_apply_compiler_warnings(minilisysm_core)

add_executable(minilisysm apps/minilisysm-agent/main.cpp)
target_link_libraries(minilisysm PRIVATE minilisysm_core)

if(MINILISYSM_BUILD_TESTS)
    enable_testing()

    add_executable(test_spsc tests/unit/test_spsc.cpp)
    target_link_libraries(test_spsc PRIVATE minilisysm_core)
    add_test(NAME spsc_queue COMMAND test_spsc)

    add_executable(test_rule_engine tests/unit/test_rule_engine.cpp)
    target_link_libraries(test_rule_engine PRIVATE minilisysm_core)
    add_test(NAME rule_engine COMMAND test_rule_engine)

    add_executable(test_factories tests/unit/test_factories.cpp)
    target_link_libraries(test_factories PRIVATE minilisysm_core)
    add_test(NAME factories COMMAND test_factories)

    add_executable(test_event_serializer tests/unit/test_event_serializer.cpp)
    target_link_libraries(test_event_serializer PRIVATE minilisysm_core)
    add_test(NAME event_serializer COMMAND test_event_serializer)

    add_executable(test_event_dispatcher tests/unit/test_event_dispatcher.cpp)
    target_link_libraries(test_event_dispatcher PRIVATE minilisysm_core)
    add_test(NAME event_dispatcher COMMAND test_event_dispatcher)

    add_executable(test_jsonl_event_sink tests/unit/test_jsonl_event_sink.cpp)
    target_link_libraries(test_jsonl_event_sink PRIVATE minilisysm_core)
    add_test(NAME jsonl_event_sink COMMAND test_jsonl_event_sink)

    add_executable(test_metrics_server tests/unit/test_metrics_server.cpp)
    target_link_libraries(test_metrics_server PRIVATE minilisysm_core)
    add_test(NAME metrics_server COMMAND test_metrics_server)

    add_executable(test_metric_registry tests/unit/test_metric_registry.cpp)
    target_link_libraries(test_metric_registry PRIVATE minilisysm_core)
    add_test(NAME metric_registry COMMAND test_metric_registry)

    add_executable(test_network_event_sink tests/unit/test_network_event_sink.cpp)
    target_link_libraries(test_network_event_sink PRIVATE minilisysm_core)
    add_test(NAME network_event_sink COMMAND test_network_event_sink)

    add_executable(test_interfaces tests/unit/test_interfaces.cpp)
    target_link_libraries(test_interfaces PRIVATE minilisysm_core)
    add_test(NAME interfaces COMMAND test_interfaces)
endif()

if(MINILISYSM_BUILD_TOOLS)
    add_executable(bench_spsc tools/bench/bench_spsc.cpp)
    target_link_libraries(bench_spsc PRIVATE minilisysm_core)
endif()
