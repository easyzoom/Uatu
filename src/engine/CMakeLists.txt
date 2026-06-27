add_library(uatu_engine
    attach_engine.cpp
)
target_link_libraries(uatu_engine
    PUBLIC  uatu_includes
            tl::expected
    PRIVATE uatu_dwarf
            uatu_ebpf
)
if(UATU_EBPF_AVAILABLE)
    target_compile_definitions(uatu_engine PRIVATE
        BPF_OBJ_PATH="${CMAKE_BINARY_DIR}/ebpf/watch.bpf.o"
    )
    add_dependencies(uatu_engine ebpf_watch_skel)
endif()
