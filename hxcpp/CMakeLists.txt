cmake_minimum_required(VERSION 3.24)
project(nova_hxcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hxcpp-runtime STATIC
    runtime/gc/runtime.cpp
    runtime/core/runtime_facade.cpp
    runtime/core/string.cpp
    runtime/core/thread.cpp
    runtime/core/cffi.cpp
    runtime/core/class.cpp
    runtime/core/array.cpp
    runtime/core/dynamic.cpp
    runtime/core/stack_trace.cpp
    runtime/core/diagnostics.cpp
    runtime/core/compatibility.cpp
    runtime/core/memory.cpp
    runtime/core/native_sys.cpp
    runtime/core/native_zlib.cpp
    runtime/core/resources.cpp
    runtime/core/weak_map.cpp
    runtime/core/native_date.cpp
    runtime/core/native_regex.cpp
    runtime/core/native_lib.cpp
    runtime/core/native_ssl.cpp
)
target_include_directories(hxcpp-runtime PUBLIC include)
target_link_libraries(hxcpp-runtime PUBLIC
    z pcre2-16 mbedtls mbedx509 mbedcrypto)
if(WIN32)
    target_link_libraries(hxcpp-runtime PUBLIC ws2_32 crypt32 bcrypt psapi)
endif()
if(MINGW)
    # Keep test/runtime executables independent of whatever libstdc++ DLL a
    # shell or Explorer happens to find first (Git for Windows commonly ships
    # an older, ABI-incompatible copy).
    target_link_options(hxcpp-runtime INTERFACE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()
target_compile_options(hxcpp-runtime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

enable_testing()
add_executable(hxcpp-gc-bootstrap-test tests/gc_bootstrap.cpp)
target_link_libraries(hxcpp-gc-bootstrap-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-bootstrap COMMAND hxcpp-gc-bootstrap-test)

add_executable(hxcpp-gc-concurrent-test tests/gc_concurrent.cpp)
target_link_libraries(hxcpp-gc-concurrent-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-concurrent COMMAND hxcpp-gc-concurrent-test)

add_executable(hxcpp-gc-parallel-remark-test
    tests/gc_parallel_remark.cpp)
target_link_libraries(hxcpp-gc-parallel-remark-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-parallel-remark
         COMMAND hxcpp-gc-parallel-remark-test)

add_executable(hxcpp-gc-foreign-attach-test tests/gc_foreign_attach.cpp)
target_link_libraries(hxcpp-gc-foreign-attach-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-foreign-attach COMMAND hxcpp-gc-foreign-attach-test)
set_tests_properties(hxcpp-gc-foreign-attach PROPERTIES TIMEOUT 15)

add_executable(hxcpp-gc-automatic-test tests/gc_automatic.cpp)
target_link_libraries(hxcpp-gc-automatic-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-automatic COMMAND hxcpp-gc-automatic-test)

add_executable(hxcpp-gc-young-test tests/gc_young.cpp)
target_link_libraries(hxcpp-gc-young-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-young COMMAND hxcpp-gc-young-test)

add_executable(hxcpp-gc-concurrent-young-test
    tests/gc_concurrent_young.cpp)
target_link_libraries(hxcpp-gc-concurrent-young-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-concurrent-young
         COMMAND hxcpp-gc-concurrent-young-test)
set_tests_properties(hxcpp-gc-concurrent-young PROPERTIES TIMEOUT 20)

add_executable(hxcpp-gc-generational-test tests/gc_generational.cpp)
target_link_libraries(hxcpp-gc-generational-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-generational COMMAND hxcpp-gc-generational-test)

add_executable(hxcpp-gc-remembered-buffer-test
    tests/gc_remembered_buffer.cpp)
target_link_libraries(hxcpp-gc-remembered-buffer-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-remembered-buffer
         COMMAND hxcpp-gc-remembered-buffer-test)

add_executable(hxcpp-gc-relocation-test tests/gc_relocation.cpp)
target_link_libraries(hxcpp-gc-relocation-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-relocation COMMAND hxcpp-gc-relocation-test)

add_executable(hxcpp-gc-in-place-relocation-test
    tests/gc_in_place_relocation.cpp)
target_link_libraries(hxcpp-gc-in-place-relocation-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-in-place-relocation
         COMMAND hxcpp-gc-in-place-relocation-test)

add_executable(hxcpp-gc-handles-test tests/gc_handles.cpp)
target_link_libraries(hxcpp-gc-handles-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-handles COMMAND hxcpp-gc-handles-test)

add_executable(hxcpp-gc-pinned-space-test tests/gc_pinned_space.cpp)
target_link_libraries(hxcpp-gc-pinned-space-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-pinned-space COMMAND hxcpp-gc-pinned-space-test)

add_executable(hxcpp-gc-references-test tests/gc_references.cpp)
target_link_libraries(hxcpp-gc-references-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-references COMMAND hxcpp-gc-references-test)

add_executable(hxcpp-gc-concurrent-relocation-test
    tests/gc_concurrent_relocation.cpp)
target_link_libraries(hxcpp-gc-concurrent-relocation-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-concurrent-relocation
         COMMAND hxcpp-gc-concurrent-relocation-test)

add_executable(hxcpp-gc-relocation-failure-test
    tests/gc_relocation_failure.cpp)
target_link_libraries(hxcpp-gc-relocation-failure-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-relocation-failure
         COMMAND hxcpp-gc-relocation-failure-test)

add_executable(hxcpp-gc-oom-test tests/gc_oom.cpp)
target_link_libraries(hxcpp-gc-oom-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-oom COMMAND hxcpp-gc-oom-test)

add_executable(hxcpp-gc-allocation-stall-test
    tests/gc_allocation_stall.cpp)
target_link_libraries(hxcpp-gc-allocation-stall-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-allocation-stall
         COMMAND hxcpp-gc-allocation-stall-test)
set_tests_properties(hxcpp-gc-allocation-stall PROPERTIES TIMEOUT 20)

add_executable(hxcpp-gc-tlab-test tests/gc_tlab.cpp)
target_link_libraries(hxcpp-gc-tlab-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-tlab COMMAND hxcpp-gc-tlab-test)

add_executable(hxcpp-gc-virtual-memory-test tests/gc_virtual_memory.cpp)
target_link_libraries(hxcpp-gc-virtual-memory-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-virtual-memory COMMAND hxcpp-gc-virtual-memory-test)

add_executable(hxcpp-gc-memory-policy-test tests/gc_memory_policy.cpp)
target_link_libraries(hxcpp-gc-memory-policy-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-memory-policy COMMAND hxcpp-gc-memory-policy-test)

add_executable(hxcpp-gc-telemetry-test tests/gc_telemetry.cpp)
target_link_libraries(hxcpp-gc-telemetry-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-telemetry COMMAND hxcpp-gc-telemetry-test)
set_tests_properties(hxcpp-gc-telemetry PROPERTIES TIMEOUT 20)

add_executable(hxcpp-gc-softmax-test tests/gc_softmax.cpp)
target_link_libraries(hxcpp-gc-softmax-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-softmax COMMAND hxcpp-gc-softmax-test)
set_tests_properties(hxcpp-gc-softmax PROPERTIES TIMEOUT 20)

add_executable(hxcpp-gc-uncommit-test tests/gc_uncommit.cpp)
target_link_libraries(hxcpp-gc-uncommit-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-uncommit COMMAND hxcpp-gc-uncommit-test)

add_executable(hxcpp-gc-large-space-test tests/gc_large_space.cpp)
target_link_libraries(hxcpp-gc-large-space-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-large-space COMMAND hxcpp-gc-large-space-test)

add_executable(hxcpp-gc-static-roots-test tests/gc_static_roots.cpp)
target_link_libraries(hxcpp-gc-static-roots-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-static-roots COMMAND hxcpp-gc-static-roots-test)

add_executable(hxcpp-gc-ephemeron-test tests/gc_ephemeron.cpp)
target_link_libraries(hxcpp-gc-ephemeron-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-ephemeron COMMAND hxcpp-gc-ephemeron-test)

add_executable(hxcpp-gc-finalizer-test tests/gc_finalizer.cpp)
target_link_libraries(hxcpp-gc-finalizer-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-finalizer COMMAND hxcpp-gc-finalizer-test)

add_executable(hxcpp-generated-abi-test tests/generated_abi.cpp)
target_link_libraries(hxcpp-generated-abi-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-generated-abi COMMAND hxcpp-generated-abi-test)

add_executable(hxcpp-string-test tests/string.cpp)
target_link_libraries(hxcpp-string-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-string COMMAND hxcpp-string-test)

add_executable(hxcpp-regex-test tests/regex.cpp)
target_link_libraries(hxcpp-regex-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-regex COMMAND hxcpp-regex-test)

add_executable(hxcpp-dynamic-test tests/dynamic.cpp)
target_link_libraries(hxcpp-dynamic-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-dynamic COMMAND hxcpp-dynamic-test)

add_executable(hxcpp-array-test tests/array.cpp)
target_link_libraries(hxcpp-array-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-array COMMAND hxcpp-array-test)

add_executable(hxcpp-gc-late-pin-test tests/gc_late_pin.cpp)
target_link_libraries(hxcpp-gc-late-pin-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-gc-late-pin COMMAND hxcpp-gc-late-pin-test)

add_executable(hxcpp-cffi-test tests/cffi.cpp)
target_link_libraries(hxcpp-cffi-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-cffi COMMAND hxcpp-cffi-test)

add_executable(hxcpp-weak-map-test tests/weak_map.cpp)
target_link_libraries(hxcpp-weak-map-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-weak-map COMMAND hxcpp-weak-map-test)

add_executable(hxcpp-thread-test tests/thread.cpp)
target_link_libraries(hxcpp-thread-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-thread COMMAND hxcpp-thread-test)

add_executable(hxcpp-thread-shutdown-test tests/thread_shutdown.cpp)
target_link_libraries(hxcpp-thread-shutdown-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-thread-shutdown COMMAND hxcpp-thread-shutdown-test)
set_tests_properties(hxcpp-thread-shutdown PROPERTIES TIMEOUT 5)

add_executable(hxcpp-stack-trace-test tests/stack_trace.cpp)
target_link_libraries(hxcpp-stack-trace-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-stack-trace COMMAND hxcpp-stack-trace-test)

add_executable(hxcpp-compatibility-test tests/compatibility.cpp)
target_link_libraries(hxcpp-compatibility-test PRIVATE hxcpp-runtime)
add_test(NAME hxcpp-compatibility COMMAND hxcpp-compatibility-test)
