# TL-SYS-C (docs/TileLink_Plan.md §4): configs/system3_tlc.csv (4 CacheMasters -> Xbar -> CoherentManager ->
# RamSlave, MESI and MSI) and configs/system4_trace.csv (the same driven by the EEMBC a2time01 traces) built
# through Kernel::build (GraphCheck, the default watchdog and CoherenceCheck registered by the kernel), a
# Monitor on every link: 0 violations, every access done.
tl_add_test(TL-SYS-C test_tlcsys)
target_compile_definitions(TL-SYS-C_test_tlcsys PRIVATE TL_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../configs"
                           TL_TRACE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../BMs/eembc-traces/a2time01-trace")
