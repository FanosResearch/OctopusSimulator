# TL-RUN (docs/TileLink_Review_Action_Plan.md problem 6): tlsim's exit status, stop reason and outputs.
tl_add_test(TL-RUN test_run)
target_compile_definitions(TL-RUN_test_run PRIVATE TLSIM_PATH="$<TARGET_FILE:tlsim>" TL_CONFIGS="${PROJECT_SOURCE_DIR}/configs" TL_RUN_OUT="${PROJECT_BINARY_DIR}/tl_run")
add_dependencies(TL-RUN_test_run tlsim)
