# TL-DET (docs/TileLink_Plan.md §6): the same configuration run 3 times gives byte-identical
# transcript and TLStats.csv; another seed gives another transcript. Runs tlsim on configs/.
tl_add_test(TL-DET test_det)
target_compile_definitions(TL-DET_test_det PRIVATE TLSIM_PATH="$<TARGET_FILE:tlsim>" TL_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../configs")
add_dependencies(TL-DET_test_det tlsim)
