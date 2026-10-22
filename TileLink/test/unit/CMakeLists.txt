# TL-UNIT (docs/TileLink_Plan.md §6): encodings, beat math, masks, parameters, config, kernel, outputs.
foreach(t test_types test_beats test_params test_config test_kernel test_outputs)
    tl_add_test(TL-UNIT ${t})
endforeach()
# tlsim smoke: the NullMaster/NullSlave system runs and writes its outputs.
add_test(NAME TL-UNIT.tlsim_null
         COMMAND tlsim -c ${CMAKE_CURRENT_SOURCE_DIR}/null_system.csv -o ${PROJECT_BINARY_DIR}/tlsim_null --trace -p "cycles(i)=50")
