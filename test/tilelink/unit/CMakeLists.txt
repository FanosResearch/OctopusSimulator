# Unit tests of the TileLink adapter (docs/TileLink_Plan.md s5.2, s5.3 I-A): TLPort through the
# library's crossbar with an echo stand-in for the protocols, the admission contract in
# CacheController, the Logger's TileLink columns, memory over TileLink (TLMemSlave, plan s5.3 I-B).
# Driven by test/tilelink/run.sh.
add_executable(Octopus_tl_tests
    main.cpp
    tl_port.cpp
    tl_admit.cpp
    tl_logger.cpp
    tl_mem.cpp
    tl_retry.cpp
)
target_link_libraries(Octopus_tl_tests Octopus_core)
target_include_directories(Octopus_tl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/test/network/unit)   # testing.h
