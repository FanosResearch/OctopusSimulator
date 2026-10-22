# TL-NEG (docs/TileLink_Plan.md §6, docs/TileLink_Negative_Tests.md §2, §3): one mutant per TL-UL/UH
# monitor rule, in report mode (exactly the expected rule id, at the documented cycle) and in fatal
# mode (a subprocess of the test binary itself: exit status 2, the expected id first).
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-NEG test_neg ${CMAKE_CURRENT_SOURCE_DIR}/test_neg.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-NEG_test_neg PRIVATE ${TL_FIX})
target_compile_definitions(TL-NEG_test_neg PRIVATE TLSIM_PATH="$<TARGET_FILE:tlsim>")
add_dependencies(TL-NEG_test_neg tlsim)
