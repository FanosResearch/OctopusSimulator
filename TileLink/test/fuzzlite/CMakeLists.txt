# TL-FUZZ-U-lite (T2): system 1 (TL-UL) and system 2 (TL-UH) of configs/ for 50k requests each,
# 10 seeds: 0 monitor violations, no watchdog trip, every request answered. The full TL-FUZZ-U
# (Xbar, Fragmenter, WidthWidget, Throttle, ValueCheck) runs once the T3 agents are in.
tl_add_test(TL-FUZZ test_fuzzlite)
target_compile_definitions(TL-FUZZ_test_fuzzlite PRIVATE TL_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../configs")
