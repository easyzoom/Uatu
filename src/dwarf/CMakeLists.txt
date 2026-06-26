find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBDW libdw)
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
