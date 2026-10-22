# TL-FUZZ-U (docs/TileLink_Plan.md §6): the full plan-§4 system 2 (4 FuzzMasters -> Xbar 4x2 ->
# {WidthWidget 8->4 -> Fragmenter -> RAM}, {RAM}), TL-UH with bursts and atomics, through tlsim with a
# Monitor on every link, the default watchdog and one end-to-end ValueCheck on the masters' links.
# Pass = exit 0 (no violation, no trip, every request answered) for each of 10 seeds.
foreach(s RANGE 1 10)
    add_test(NAME TL-FUZZ-U.system2_seed${s}
             COMMAND tlsim -c ${PROJECT_SOURCE_DIR}/configs/system2_uh_full.csv -o ${PROJECT_BINARY_DIR}/fuzz_u_${s} -p "seed(i)=${s}")
    set_tests_properties(TL-FUZZ-U.system2_seed${s} PROPERTIES PASS_REGULAR_EXPRESSION "value check: [0-9]+ reads.* 0 violations")
endforeach()
