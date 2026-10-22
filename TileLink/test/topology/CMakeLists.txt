# TL-TOPO (docs/TileLink_Plan.md §3.5, §3.6, §6): the topology agents (Xbar, Buffer, WidthWidget,
# Fragmenter, SourceShrinker, Throttle) and ValueCheck, with the tests' own scripted agents
# (TopoTest.h). Includes TL-ADAPT-ERR (test_fragmenter, test_widthwidget), TL-ID (test_shrinker test_valuecheck test_system)
# and a TL-FUZZ-U-style system run (test_system).
foreach(t test_xbar test_buffer test_widthwidget test_fragmenter test_shrinker test_valuecheck test_system)
    tl_add_test(TL-TOPO ${t})
endforeach()
