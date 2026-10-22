# TL-NEG-C (docs/TileLink_Plan.md §6 TL-NEG, docs/TileLink_Negative_Tests.md §4): one mutant per TL-C
# monitor rule (31), the doc's controls, and the Release-vs-GrantAck case of docs/TileLink_TLC_Tables.md
# §11 item 5, in report mode (exactly the expected rule id at the documented cycle) and in fatal mode (a
# subprocess of the test binary itself: exit status 2, the expected id first; a control exits 0).
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-NEG-C test_negc ${CMAKE_CURRENT_SOURCE_DIR}/test_negc.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-NEG-C_test_negc PRIVATE ${TL_FIX})
