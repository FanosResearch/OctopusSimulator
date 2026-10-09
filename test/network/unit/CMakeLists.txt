# Unit tests of the network (docs/NoC_Test_Plan.md). One source file per step; the
# executable links the simulator's core library so tests drive the real classes.
add_executable(Octopus_tests
    main.cpp
    step1_foundations.cpp
    step2_routing.cpp
)
target_link_libraries(Octopus_tests Octopus_core)
target_include_directories(Octopus_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
