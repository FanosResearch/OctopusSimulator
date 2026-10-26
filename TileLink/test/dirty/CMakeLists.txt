# TL-DIRTY (docs/TileLink_LLC_Dirty_Writeback_Plan.md DW-1 to DW-5): the v2 manager tables write a block back
# only when the LLC's copy is dirty: a clean eviction, ReleaseData, ProbeAckData, a dirty copy surviving a
# clean Release, and an uncached Put performed in the LLC; MESI and MSI.
tl_add_test(TL-DIRTY test_dirty)
target_compile_definitions(TL-DIRTY_test_dirty PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
