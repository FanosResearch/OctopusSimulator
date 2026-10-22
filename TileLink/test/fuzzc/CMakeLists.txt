# TL-FUZZ-C (docs/TileLink_Plan.md §6): 2, 4 and 8 CacheMasters on a small address set (high
# contention), Throttles on every link, Monitor on every link, CoherenceCheck, ValueCheck
# on the outer link, the default watchdog; 10 seeds, MESI and MSI managers, direct links and an Xbar.
tl_add_test(TL-FUZZ-C test_fuzzc)
target_compile_definitions(TL-FUZZ-C_test_fuzzc PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
