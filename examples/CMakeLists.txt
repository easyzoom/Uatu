add_executable(example_01_basic    01_basic/main.cpp)
add_executable(example_02_exception 02_exception/main.cpp)
add_executable(example_03_slow_path 03_slow_path/main.cpp)

foreach(ex example_01_basic example_02_exception example_03_slow_path)
    target_compile_options(${ex} PRIVATE -g -O0)
    target_link_libraries(${ex} PRIVATE uatu_includes)
endforeach()
