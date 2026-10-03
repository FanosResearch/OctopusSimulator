# Octopus hands-on tutorial

Start with `00-setup-and-first-run` and go in order. Each exercise folder has the same
shape:

```
README.md    what to do, the exact commands, what you should see
check.sh     validates the exercise result (00 checks your saved output/)
expected/    the reference result check.sh compares against
start.sh     (only where needed) puts the starting files in place
```

The simulator is **deterministic and platform-independent**: the same inputs give the
same cycle counts on Linux, Windows and in a codespace, down to the last digit. So
`check.sh` is an equality test, not a smoke test — if your numbers differ from the
reference, something real changed (usually a file under `configuration/`).

| # | folder | what you do |
|---|---|---|
| 00 | `00-setup-and-first-run/` | verify the environment, run one benchmark, read its output, open the visualizer |
| 01 | `01-exploration/` | change one thing at a time — arbiter, memory model, protocol — and watch one number move |
| 02 | `02-configuration/` | how the CSV configuration model works, and why every knob is a command-line override |
| 03 | `03-extending-octopus/` | two ways to extend the simulator: a new arbiter or a protocol change |
| 04 | `04-gem5-and-fullsystem-stack/` | Octopus as the memory hierarchy behind gem5, in SE mode and full-system ARM |

The reference for everything the exercises touch is the manual,
[`docs/manual/octopus-manual.pdf`](../docs/manual/octopus-manual.pdf): its chapter 15 is this
tutorial, and each exercise's README names the chapters it draws on.

Two commands to remember:

```shell
bash scripts/check_environment.sh      # is my environment healthy?
git checkout -- configuration/         # put every configuration file back as shipped
```
