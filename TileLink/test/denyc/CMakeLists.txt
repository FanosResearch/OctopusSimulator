# TL-DENY, TL-C part (docs/TileLink_Plan.md §6): an ErrorSlave behind the manager and behind an outer
# crossbar: denied Grants (GrantAck still sent, permission unchanged, the CPU access completes with an
# error), denied uncached Get/Put/Atomic (data responses corrupt on every beat), Monitor clean.
tl_add_test(TL-DENY-C test_denyc)
target_compile_definitions(TL-DENY-C_test_denyc PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
