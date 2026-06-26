add_executable(uatu
    main.cpp
    formatter.cpp
)
target_link_libraries(uatu PRIVATE
    uatu_includes
    uatu_engine
)
