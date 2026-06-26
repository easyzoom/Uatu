# Prefer vendored libbpf 1.x static library (third_party/libbpf) over the
# system libbpf, which on Ubuntu 22.04 is 0.5.0 and incompatible with
# kernels >= 6.x (BTF format mismatch).
set(_VENDORED_LIBBPF_A   "${CMAKE_BINARY_DIR}/libbpf/libbpf.a")
set(_VENDORED_LIBBPF_INC "${CMAKE_SOURCE_DIR}/third_party/libbpf/src")

if(EXISTS "${_VENDORED_LIBBPF_A}" AND EXISTS "${_VENDORED_LIBBPF_INC}/libbpf.h")
    message(STATUS "libbpf: using vendored static library (1.x) at ${_VENDORED_LIBBPF_A}")
    set(LIBBPF_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/libbpf/include")
    set(LIBBPF_LIBRARIES    "${_VENDORED_LIBBPF_A}" elf z)
else()
    # Fallback: system libbpf (may be too old for newer kernels)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBBPF libbpf)
    endif()

    if(NOT LIBBPF_FOUND)
        execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_RELEASE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(_KHEADERS_LIBBPF_INC
            "/usr/src/linux-headers-${KERNEL_RELEASE}/tools/bpf/resolve_btfids/libbpf/include"
        )
        if(EXISTS "${_KHEADERS_LIBBPF_INC}/bpf/libbpf.h")
            set(LIBBPF_INCLUDE_DIRS "${_KHEADERS_LIBBPF_INC}")
            message(STATUS "libbpf headers found via kernel headers: ${_KHEADERS_LIBBPF_INC}")
        endif()
        find_library(LIBBPF_LIB NAMES bpf libbpf
            HINTS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)
        if(NOT LIBBPF_LIB AND EXISTS "/usr/lib/x86_64-linux-gnu/libbpf.so.0")
            set(LIBBPF_LIB "/usr/lib/x86_64-linux-gnu/libbpf.so.0")
        endif()
        if(LIBBPF_LIB)
            set(LIBBPF_LIBRARIES "${LIBBPF_LIB}")
            message(STATUS "libbpf library (system fallback): ${LIBBPF_LIB}")
        else()
            message(WARNING "libbpf library not found — uatu_ebpf will link without it")
        endif()
    endif()
endif()

add_library(uatu_ebpf
    uprobe_loader.cpp
)
target_link_libraries(uatu_ebpf
    PUBLIC  uatu_includes
    PRIVATE ${LIBBPF_LIBRARIES}
)
target_include_directories(uatu_ebpf
    PRIVATE ${LIBBPF_INCLUDE_DIRS}
)
if(UATU_EBPF_AVAILABLE)
    target_compile_definitions(uatu_ebpf PRIVATE
        BPF_OBJ_PATH="${CMAKE_BINARY_DIR}/ebpf/watch.bpf.o"
    )
    add_dependencies(uatu_ebpf ebpf_watch_skel)
endif()
