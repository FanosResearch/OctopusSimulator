# TL-CMO (docs/TileLink_Plan.md §6, §3.5; spec §8.2.3): CBOInval, CBOClean, CBOFlush against 1-3 holders,
# clean and dirty, racing an Acquire and a voluntary Release; HintAck only after the outer AccessAck and
# the inner ProbeAcks; permissions after N / <=B / N; memory contents; a denied outer write.
tl_add_test(TL-CMO test_cmo)
target_compile_definitions(TL-CMO_test_cmo PRIVATE TL_TABLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tables")
