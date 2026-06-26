find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBBPF libbpf)
endif()

# Fallback: locate libbpf headers from kernel linux-headers when pkg-config
# finds nothing (e.g. libbpf runtime is installed but -dev package is absent).
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
    # Library: prefer unversioned symlink; fall back to versioned .so.0
    find_library(LIBBPF_LIB NAMES bpf libbpf
        HINTS /usr/lib/x86_64-linux-gnu
              /usr/lib
              /usr/local/lib
    )
    if(NOT LIBBPF_LIB)
        # libbpf runtime installed without -dev; link directly against .so.0
        if(EXISTS "/usr/lib/x86_64-linux-gnu/libbpf.so.0")
            set(LIBBPF_LIB "/usr/lib/x86_64-linux-gnu/libbpf.so.0")
        endif()
    endif()
    if(LIBBPF_LIB)
        set(LIBBPF_LIBRARIES "${LIBBPF_LIB}")
        message(STATUS "libbpf library: ${LIBBPF_LIB}")
    else()
        message(WARNING "libbpf library not found — uatu_ebpf will link without it")
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
