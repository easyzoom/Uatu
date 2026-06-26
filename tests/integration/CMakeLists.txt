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

add_test(NAME integration_trace COMMAND test_trace)
add_test(NAME integration_stack COMMAND test_stack)
# integration_watch requires CAP_BPF; SKIPS gracefully when unavailable
add_test(NAME integration_watch COMMAND test_watch)
