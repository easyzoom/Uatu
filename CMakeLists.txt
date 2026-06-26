cmake_minimum_required(VERSION 3.20)
project(uatu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBBPF libbpf)
    pkg_check_modules(LIBDW  libdw)
endif()

find_package(nlohmann_json CONFIG QUIET)
find_package(replxx        CONFIG QUIET)
find_package(GTest         CONFIG QUIET)
find_package(tl-expected   CONFIG QUIET)

# Interface library to share include path across all targets
add_library(uatu_includes INTERFACE)
target_include_directories(uatu_includes INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# eBPF compilation (optional — skipped if clang/bpftool not found)
find_program(CLANG clang)
find_program(BPFTOOL bpftool)

# Locate BPF helper headers for clang -target bpf compilation.
# Prefer /usr/include/bpf (libbpf-dev); fall back to kernel linux-headers.
if(EXISTS "/usr/include/bpf/bpf_helpers.h")
    set(BPF_HELPER_INCLUDE_DIR "/usr/include/bpf")
else()
    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE _KREL
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(_KHEADERS_BPF_INC
        "/usr/src/linux-headers-${_KREL}/tools/bpf/resolve_btfids/libbpf/include/bpf"
    )
    if(EXISTS "${_KHEADERS_BPF_INC}/bpf_helpers.h")
        set(BPF_HELPER_INCLUDE_DIR "${_KHEADERS_BPF_INC}/..")
        message(STATUS "BPF helper headers found via kernel headers: ${BPF_HELPER_INCLUDE_DIR}")
    endif()
endif()

# Detect BPF target architecture from the cross-compilation target (or native host)
if(NOT DEFINED BPF_TARGET_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(BPF_TARGET_ARCH "x86")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(BPF_TARGET_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(BPF_TARGET_ARCH "riscv")
    else()
        set(BPF_TARGET_ARCH "x86")
        message(WARNING "Unknown arch '${CMAKE_SYSTEM_PROCESSOR}' — defaulting BPF target to x86")
    endif()
endif()
message(STATUS "BPF target architecture: ${BPF_TARGET_ARCH}")

if(CLANG AND BPFTOOL AND EXISTS "${CMAKE_SOURCE_DIR}/ebpf/vmlinux.h" AND BPF_HELPER_INCLUDE_DIR)
    set(BPF_SRC  "${CMAKE_SOURCE_DIR}/ebpf/watch.bpf.c")
    set(BPF_OBJ  "${CMAKE_BINARY_DIR}/ebpf/watch.bpf.o")
    set(BPF_SKEL "${CMAKE_BINARY_DIR}/ebpf/watch.skel.h")

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ebpf")

    add_custom_command(
        OUTPUT  "${BPF_OBJ}"
        COMMAND "${CLANG}" -g -O2 -target bpf
                -D__TARGET_ARCH_${BPF_TARGET_ARCH}
                -I"${CMAKE_SOURCE_DIR}/ebpf"
                -I"${BPF_HELPER_INCLUDE_DIR}"
                -c "${BPF_SRC}" -o "${BPF_OBJ}"
        DEPENDS "${BPF_SRC}" "${CMAKE_SOURCE_DIR}/ebpf/vmlinux.h"
        COMMENT "Compiling watch.bpf.c (target arch: ${BPF_TARGET_ARCH})"
    )

    add_custom_command(
        OUTPUT  "${BPF_SKEL}"
        COMMAND "${BPFTOOL}" gen skeleton "${BPF_OBJ}" name watch
                > "${BPF_SKEL}"
        DEPENDS "${BPF_OBJ}"
        COMMENT "Generating watch.skel.h"
    )

    add_custom_target(ebpf_watch_skel ALL DEPENDS "${BPF_SKEL}")
    set(UATU_EBPF_AVAILABLE TRUE CACHE BOOL "eBPF support available" FORCE)
else()
    message(STATUS "eBPF toolchain not found — skipping eBPF compilation")
    set(UATU_EBPF_AVAILABLE FALSE CACHE BOOL "eBPF support available" FORCE)
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)
