# TL-FLOWS (docs/TileLink_Plan.md §6): Figures 29 and 30 of the spec as directed races with scripted
# timing (docs/TileLink_TLC_Tables.md §5.1, §5.2): the documented serialization, the Grant type (data after
# a probe), the values carried; Monitor and CoherenceCheck clean.
tl_add_test(TL-FLOWS test_flows)
target_compile_definitions(TL-FLOWS_test_flows PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
