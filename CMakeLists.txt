cmake_minimum_required(VERSION 3.16)
project(minilisysm VERSION 0.1.0 LANGUAGES CXX)

include(cmake/MinilisysmOptions.cmake)
include(cmake/MinilisysmCompilerWarnings.cmake)
include(cmake/MinilisysmDependencies.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})

add_library(minilisysm_core
    src/core/config.cpp
    src/core/time.cpp
    src/collectors/collector_factory.cpp
    src/collectors/cpu_usage_collector.cpp
    src/collectors/hardware_health_collector.cpp
    src/collectors/io_delay_collector.cpp
    src/collectors/linux_proc_reader.cpp
    src/collectors/meminfo_collector.cpp
    src/collectors/sched_delay_collector.cpp
    src/collectors/self_status_collector.cpp
    src/rules/rule_factory.cpp
    src/rules/rule_engine.cpp
    src/runtime/event_dispatcher.cpp
    src/runtime/metric_registry.cpp
    src/runtime/monitor_metrics.cpp
    src/runtime/metrics_server.cpp
    src/runtime/monitor.cpp
    src/runtime/thread_policy.cpp
    src/runtime/whitelisted_process_reader.cpp
    src/storage/event_serializer.cpp
    src/storage/jsonl_event_sink.cpp
    src/storage/network_event_sink.cpp
    src/storage/storage_factory.cpp
)

target_include_directories(minilisysm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(minilisysm_core PUBLIC spdlog::spdlog)

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

if(MINILISYSM_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(minilisysm_core PUBLIC -O0 -g --coverage)
        target_link_options(minilisysm_core PUBLIC --coverage)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(minilisysm_core PUBLIC -fprofile-update=atomic)
        endif()
    else()
        message(FATAL_ERROR "MINILISYSM_ENABLE_COVERAGE requires GCC or Clang")
    endif()
endif()

add_executable(minilisysm apps/minilisysm-agent/main.cpp)
target_link_libraries(minilisysm PRIVATE minilisysm_core)
target_compile_definitions(minilisysm PRIVATE
    MINILISYSM_DEFAULT_CONFIG_PATH="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/minilisysm/lisysm_monitor.ini"
)

install(TARGETS minilisysm
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS scripts/qalog
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES configs/lisysm_monitor.ini
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/minilisysm
)

install(FILES
    README.md
    docs/ARCHITECTURE.md
    docs/DEPENDENCIES.md
    docs/DEPLOYMENT.md
    docs/EBPF.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/minilisysm
)

if(MINILISYSM_BUILD_TESTS)
    enable_testing()

    function(minilisysm_add_unit_test target test_name source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE minilisysm_core)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
        )
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    minilisysm_add_unit_test(test_spsc spsc_queue tests/unit/test_spsc.cpp)
    minilisysm_add_unit_test(test_rule_engine rule_engine tests/unit/test_rule_engine.cpp)
    minilisysm_add_unit_test(test_factories factories tests/unit/test_factories.cpp)
    minilisysm_add_unit_test(test_event_serializer event_serializer tests/unit/test_event_serializer.cpp)
    minilisysm_add_unit_test(test_event_dispatcher event_dispatcher tests/unit/test_event_dispatcher.cpp)
    minilisysm_add_unit_test(test_monitor monitor tests/unit/test_monitor.cpp)
    minilisysm_add_unit_test(test_whitelisted_process_reader whitelisted_process_reader tests/unit/test_whitelisted_process_reader.cpp)
    target_include_directories(test_whitelisted_process_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    minilisysm_add_unit_test(test_jsonl_event_sink jsonl_event_sink tests/unit/test_jsonl_event_sink.cpp)
    minilisysm_add_unit_test(test_metrics_server metrics_server tests/unit/test_metrics_server.cpp)
    minilisysm_add_unit_test(test_metric_registry metric_registry tests/unit/test_metric_registry.cpp)
    minilisysm_add_unit_test(test_network_event_sink network_event_sink tests/unit/test_network_event_sink.cpp)
    minilisysm_add_unit_test(test_interfaces interfaces tests/unit/test_interfaces.cpp)
    minilisysm_add_unit_test(test_meminfo_collector meminfo_collector tests/unit/test_meminfo_collector.cpp)
    minilisysm_add_unit_test(test_self_status_collector self_status_collector tests/unit/test_self_status_collector.cpp)
    minilisysm_add_unit_test(test_cpu_usage_collector cpu_usage_collector tests/unit/test_cpu_usage_collector.cpp)
    minilisysm_add_unit_test(test_hardware_health_collector hardware_health_collector tests/unit/test_hardware_health_collector.cpp)
    minilisysm_add_unit_test(test_io_delay_collector io_delay_collector tests/unit/test_io_delay_collector.cpp)
    minilisysm_add_unit_test(test_sched_delay_collector sched_delay_collector tests/unit/test_sched_delay_collector.cpp)

    add_executable(test_sigterm_shutdown tests/integration/test_sigterm_shutdown.cpp)
    target_link_libraries(test_sigterm_shutdown PRIVATE Threads::Threads)
    add_dependencies(test_sigterm_shutdown minilisysm)
    set_target_properties(test_sigterm_shutdown PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
    add_test(NAME sigterm_shutdown COMMAND test_sigterm_shutdown $<TARGET_FILE:minilisysm>)
    set_tests_properties(sigterm_shutdown PROPERTIES
        TIMEOUT 15
        SKIP_RETURN_CODE 77
    )

    add_executable(test_process_memory_growth tests/integration/test_process_memory_growth.cpp)
    target_link_libraries(test_process_memory_growth PRIVATE minilisysm_core Threads::Threads)
    set_target_properties(test_process_memory_growth PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
    add_test(NAME process_memory_growth COMMAND test_process_memory_growth)
    set_tests_properties(process_memory_growth PROPERTIES
        TIMEOUT 15
        SKIP_RETURN_CODE 77
    )
endif()

if(MINILISYSM_BUILD_TOOLS)
    add_executable(bench_spsc tools/bench/bench_spsc.cpp)
    target_link_libraries(bench_spsc PRIVATE minilisysm_core)
    set_target_properties(bench_spsc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
endif()
