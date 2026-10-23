# TL-MASK (docs/TileLink_Review_Action_Plan.md problem 5): every mask lane checked, widths up to 4096.
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-MASK test_mask ${CMAKE_CURRENT_SOURCE_DIR}/test_mask.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-MASK_test_mask PRIVATE ${TL_FIX} ${TL_TEST_ROOT}/graph)
