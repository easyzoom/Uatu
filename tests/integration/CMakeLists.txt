find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest REQUIRED)
endif()

add_executable(test_watch test_watch.cpp)
target_link_libraries(test_watch PRIVATE
    uatu_includes
    uatu_engine
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(test_watch PRIVATE
    FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/fixtures"
)

add_executable(test_trace test_trace.cpp)
target_link_libraries(test_trace PRIVATE
    uatu_includes
    uatu_engine
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(test_trace PRIVATE
    FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/fixtures"
)

add_executable(test_stack test_stack.cpp)
target_link_libraries(test_stack PRIVATE
    uatu_includes
    uatu_engine
    GTest::gtest
    GTest::gtest_main
)
target_compile_definitions(test_stack PRIVATE
    FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/fixtures"
)
