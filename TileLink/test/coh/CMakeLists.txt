# TL-COH-ENGINE (docs/TileLink_Plan.md §3.7): the FSM CSV reader (coh::Table) and the policy engine's
# decoders and builders (coh::L1Policy, coh::ManagerPolicy), pure logic. Every cell of the three tables is
# compared with the cell list of docs/TileLink_TLC_Tables.md §2.4 / §3.4.
tl_add_test(TL-COH-ENGINE test_table)
tl_add_test(TL-COH-ENGINE test_policy)
foreach(t test_table test_policy)
    target_compile_definitions(TL-COH-ENGINE_${t} PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables"
                               TL_DOC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../docs")
endforeach()
