find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest REQUIRED)
endif()

add_executable(test_symbol_finder test_symbol_finder.cpp)
target_link_libraries(test_symbol_finder PRIVATE
    uatu_includes
    uatu_dwarf
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(test_symbol_finder PRIVATE
    FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/fixtures"
)

add_executable(test_json_codec test_json_codec.cpp)
target_link_libraries(test_json_codec PRIVATE
    uatu_includes
    uatu_protocol
    GTest::gtest
    GTest::gtest_main
)
