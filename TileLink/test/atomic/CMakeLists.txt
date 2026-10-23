# TL-ATOMIC (docs/TileLink_Review_Action_Plan.md problem 4): the whole-operand atomics (Atomic.h) against literal
# vectors, and the real RamSlave and CoherentManager against ValueCheck end to end (Full and Stamp data).
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-ATOMIC test_atomic ${CMAKE_CURRENT_SOURCE_DIR}/test_atomic.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-ATOMIC_test_atomic PRIVATE ${TL_FIX})
target_compile_definitions(TL-ATOMIC_test_atomic PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
