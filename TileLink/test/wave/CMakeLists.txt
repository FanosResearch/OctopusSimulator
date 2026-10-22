# TL-WAVE (docs/TileLink_Plan.md §6): the spec's waveform figures rebuilt as directed scenarios
# (test/golden/scenarios.md), compared beat for beat with the goldens.
set(TL_FIX ${TL_TEST_ROOT}/fixtures)
tl_add_test(TL-WAVE test_wave ${CMAKE_CURRENT_SOURCE_DIR}/test_wave.cpp ${TL_FIX}/ScriptedMaster.cpp ${TL_FIX}/ScriptedSlave.cpp)
target_include_directories(TL-WAVE_test_wave PRIVATE ${TL_FIX})
target_compile_definitions(TL-WAVE_test_wave PRIVATE TL_GOLDEN_DIR="${TL_TEST_ROOT}/golden")
