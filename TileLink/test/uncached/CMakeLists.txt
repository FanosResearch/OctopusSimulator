# TL-UNCACHED (docs/TileLink_Plan.md §6, §8 decision 8): an uncached FuzzMaster (Get, Put, PutPartial, atomics)
# beside 2 and 4 CacheMasters on one manager, full data, high contention; every Put/Atomic acknowledged only
# with no cached copy left, every Get with no Tip left; CoherenceCheck (values) clean; the directed cases.
tl_add_test(TL-UNCACHED test_uncached)
target_compile_definitions(TL-UNCACHED_test_uncached PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
