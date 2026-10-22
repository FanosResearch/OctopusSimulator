# TL-PROGRESS (docs/TileLink_Plan.md §6): the four §5.2 examples against the watchdog (plan §3.6),
# with the bounds of docs/TileLink_Negative_Tests.md §6. The fatal verdicts run in a child process
# (../graph/Subprocess.h) and check the exit status 2 and the first dump line.
tl_add_test(TL-PROGRESS test_progress)
