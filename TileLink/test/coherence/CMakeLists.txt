# TL-COH (docs/TileLink_Plan.md §3.6 CoherenceCheck, §6 TL-FUZZ-C): CoherenceCheck unit tests with
# scripted masters and scripted manager halves (test/fixtures): C-SWMR, C-GRANT, the denied-Grant
# exemption, C-VALUE, report and fatal modes (a child process: exit 2), the leaf-link rule, and
# chaining with a Monitor and a ValueCheck on the same link.
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-COH test_coherence ${CMAKE_CURRENT_SOURCE_DIR}/test_coherence.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-COH_test_coherence PRIVATE ${TL_FIX} ${TL_TEST_ROOT}/graph)
