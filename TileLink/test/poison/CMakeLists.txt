# TL-POISON (docs/TileLink_Review_Action_Plan.md problem 2): corruption survives the TL-C agents' storage,
# per byte where the agents hold it, per beat on the wire (§4.4).
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-POISON test_poison ${CMAKE_CURRENT_SOURCE_DIR}/test_poison.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-POISON_test_poison PRIVATE ${TL_FIX})
target_compile_definitions(TL-POISON_test_poison PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
