find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    if(CMAKE_CROSSCOMPILING)
        # When cross-compiling, pkg-config defaults to the host search path.
        # Redirect it to the target-architecture library directory derived
        # from the C compiler name (e.g. aarch64-linux-gnu-gcc → aarch64-linux-gnu).
        get_filename_component(_CC_BASENAME "${CMAKE_C_COMPILER}" NAME)
        string(REGEX MATCH "([a-z0-9_]+-linux-[a-z0-9_]+)" _TARGET_TRIPLE "${_CC_BASENAME}")
        if(_TARGET_TRIPLE)
            set(ENV{PKG_CONFIG_PATH}        "/usr/lib/${_TARGET_TRIPLE}/pkgconfig")
            set(ENV{PKG_CONFIG_LIBDIR}      "/usr/lib/${_TARGET_TRIPLE}/pkgconfig")
            set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
        endif()
    endif()
    pkg_check_modules(LIBDW libdw)
endif()

if(NOT LIBDW_FOUND)
    # Fallback: locate libdw/libelf directly without pkg-config
    find_library(LIBDW_LIB  NAMES dw)
    find_library(LIBELF_LIB NAMES elf)
    find_path(LIBDW_INC elfutils/libdw.h)
    if(LIBDW_LIB AND LIBDW_INC)
        set(LIBDW_LIBRARIES    "${LIBDW_LIB}")
        set(LIBDW_INCLUDE_DIRS "${LIBDW_INC}")
        if(LIBELF_LIB)
            list(APPEND LIBDW_LIBRARIES "${LIBELF_LIB}")
        endif()
        message(STATUS "libdw: found via find_library (${LIBDW_LIB})")
    else()
        message(WARNING "libdw not found — DWARF symbol resolution will not link")
    endif()
endif()

add_library(uatu_dwarf
    symbol_finder.cpp
)
target_link_libraries(uatu_dwarf
    PUBLIC  uatu_includes
    PRIVATE ${LIBDW_LIBRARIES}
)
target_include_directories(uatu_dwarf PRIVATE ${LIBDW_INCLUDE_DIRS})
target_compile_options(uatu_dwarf PRIVATE ${LIBDW_CFLAGS_OTHER})
