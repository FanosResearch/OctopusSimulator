# Octopus Network-on-Chip — implementation report

Running record of the implementation of `docs/NoC_Architecture_Spec.md`, step by step as laid out
in `docs/NoC_Test_Plan.md`. One section per step: what was built, the decisions taken where the
specification left room, what the tests measured, and the commit. Written on branch `network`,
off `esweek-tutorial` at `3e4add57`.

## Step 0 — infrastructure

**Built**

- `test/network/unit/`: a 60-line test framework (`testing.h`: `TEST`, `CHECK`, `CHECK_EQ`,
  prefix filter, `PASS`/`FAIL` lines) and the `Octopus_tests` executable, a CMake target linking
  `Octopus_core` so tests drive the real classes. One source file per step; step 1's is empty for
  now so the target builds.
- `test/network/run.sh`: the single runner. Builds, runs the regression suite and the requested
  step's tests, writes `test/network/out/results.csv` (id, status, measured, expected, seconds),
  exits non-zero on any `FAIL`. `XFAIL` lines are reported separately.
- `test/network/asan.sh`: the AddressSanitizer + UBSan build under WSL into `build_asan/`.
- Preset placeholders with the specification's keys and defaults: `configuration/Interconnect/
  Network.csv` (every key of spec §4 with its default, so later steps only change values),
  `MultiCoreSystem_Grid.csv` (a copy of the mesh preset until step 5) and `NetworkOnly.csv`
  (filled at step 4).
- References under `test/network/ref/`: `legacy-mesh/<trace>.md5` for the ten EEMBC traces on
  the legacy single-switch NoC, `reg-tdm.md5` for the TDM bus run, and a committed copy of the
  shipped `MultiCoreSystem.csv`.

**Decisions**

- The regression runs do not read the working copy's `MultiCoreSystem.csv`, which carries local
  edits on this machine; they pass the committed copy with `-c`, which the CLI accepts as a path.
  So the suite is independent of whatever the user is experimenting with in `configuration/`.
- Outputs go through `-o` into `test/network/out/`, never into `BMs/`.
- Checksums strip carriage returns, so references taken on Windows compare equal on Linux.
- `REG-MESH` runs its ten traces in parallel; they are independent processes with separate
  output directories.

**Tests** (`bash test/network/run.sh 0`, Windows, MinGW Release build; the sanitizer build is the
WSL g++ 15 Debug build with `-fsanitize=address,undefined`):

| id | status | measured | seconds |
|---|---|---|---|
| REG-BUS | PASS | identical | 4.5 |
| REG-ENV | PASS | 15364/377 | 5.8 |
| REG-MESH | PASS | identical | 10.3 |
| REG-TDM | PASS | identical | 4.6 |
| S0-HARNESS | PASS | - | 0 |
| S0-RUNNER | PASS | - | 0 |
| S0-REF | PASS | - | 0 |
| S0-ASAN | PASS | - | 0 |
| S0-PRESETS | PASS | - | 0 |

The legacy mesh references cover all ten EEMBC workloads on `MultiCoreSystem_Mesh` (directory MSI
on the single-switch NoC), four cores each, about ten seconds for the ten in parallel.

**Commit**: `ede914fa` "NoC step 0: test harness, runner, references, preset placeholders".

## Step 1 — foundations

**Built**

- `header/Interconnect/Network/NetTypes.h`: the class constants, `Packet` and `Flit`, the flit-count
  formula of spec §6.2, `ConfigError`, and the declaration of `fatal()`.
- `Classifier` (`Classifier.h/.cpp`): the three-class rules keyed on `Message::kind` with the
  fallback rules and the fallback counter; the legacy two-class rule `data == NULL ? 0 : 1`;
  `classifyOrFatal` and a `describe()` that prints a message's fields for the fatal path.
- `Topology` (`Topology.h/.cpp`): generators `grid2d`, `torus2d`, `ring`, `star`, and `custom`
  from a connection matrix; fixed port numbering (east, west, north, south, then local ports;
  clockwise, counter-clockwise for rings; matrix-row order for custom); far-end port resolution;
  endpoint attachment with `local_ports = 0` meaning one port per endpoint and sharing otherwise;
  `describe()` for the hang dump and messages.
- `Fatal.cpp`: the single fatal path, `Network: <message>` on stderr, exit status 2.
- Registered in `src/Interconnect/CMakeLists.txt` and the include path; the unit tests in
  `test/network/unit/step1_foundations.cpp`, run by `steps/step1.sh`.

**Decisions**

- The class constants are `CLASS_REQUEST`, `CLASS_FORWARD`, `CLASS_RESPONSE` rather than the
  spec's bare names, because `MessageType::REQUEST` already exists at global scope in
  `CommunicationInterface.h` and the bare name was ambiguous in every file that includes it.
- **Custom matrices give every endpoint an implicit router of its own.** The spec says nodes
  outside `component_ids` are routers; it is silent on endpoint-to-endpoint edges, which the
  shipped `Mesh.csv` (a complete graph of endpoints, no switch) is made of. Giving each endpoint
  node an implicit router with the endpoint on its local port keeps both shipped matrices loadable
  and makes the legacy direct-link mesh a network of five routers with one endpoint each. The
  legacy equivalence of step 6 uses `star`, where endpoints attach to one router directly, so this
  choice does not affect it. Recorded as a deviation to fold back into the spec.
- Construction errors are `ConfigError` exceptions carrying the message; the `Network` object will
  turn them into `fatal()`. This is what lets the unit tests check refusal messages without a
  child process. The exit-status side of `S1-CLS-FATAL` is therefore covered by the shell tests of
  step 4 rather than by a unit test.
- The classifier's fallback counter is incremented before the fallback rules and decremented again
  when none matches, so it counts only messages that were actually classified by a fallback.
- A torus needs at least 3 routers per dimension, otherwise the wrap link would coincide with the
  direct link; the generator refuses smaller dimensions.
- Port names (`east`, `west(wrap)`, `to 10`, `local0`) are kept per port for the hang dump and the
  error messages of the later steps.

**Tests** (`bash test/network/run.sh 1`; the same 17 tests also pass under the WSL sanitizer build,
`build_asan/test/network/unit/Octopus_tests S1`):

| id | status |
|---|---|
| S1-CLS-3 | None |
| S1-CLS-2 | None |
| S1-CLS-FATAL | None |
| S1-CLS-LOG | None |
| S1-FLITS | None |
| S1-GRID-3x3 | None |
| S1-GRID-COORD | None |
| S1-TORUS | None |
| S1-RING | None |
| S1-STAR | None |
| S1-CUSTOM-MESH | None |
| S1-CUSTOM-NOC | None |
| S1-CUSTOM-ASYM | None |
| S1-ATTACH | None |
| S1-ATTACH-MISSING | None |
| S1-ATTACH-SHARED | None |
| S1-KEYS | None |

Regression suite: REG-BUS, REG-ENV, REG-MESH, REG-TDM all identical.
