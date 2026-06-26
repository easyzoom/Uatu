# target_debug: 完整 DWARF，用于 watch/trace/stack 主要测试
add_executable(target_debug target.cpp)
target_compile_options(target_debug PRIVATE -g -O0)
target_link_libraries(target_debug PRIVATE uatu_includes)

# target_opt: 优化编译，测试 inline 边界行为
add_executable(target_opt target.cpp)
target_compile_options(target_opt PRIVATE -g -O2)
target_link_libraries(target_opt PRIVATE uatu_includes)

# target_strip: 无 DWARF，测试地址模式退化
add_executable(target_strip target.cpp)
target_compile_options(target_strip PRIVATE -g -O0)
target_link_libraries(target_strip PRIVATE uatu_includes)
add_custom_command(TARGET target_strip POST_BUILD
    COMMAND strip --strip-debug $<TARGET_FILE:target_strip>)
