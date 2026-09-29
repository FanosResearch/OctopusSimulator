# 02 — The configuration model

**Goal:** understand *why* configuration looks the way it does, so the extension
exercises make sense. Ten minutes, mostly reading. Nothing to build.

## 1. Hierarchy and inheritance

Open these, in order:

```
configuration/CacheControllers/BaseController.csv          defaults every controller shares
configuration/CacheControllers/CacheController.csv         "Extends,BaseController" + overrides
configuration/CacheControllers/CacheControllerExclusive.csv
configuration/SystemConfigurations/MultiCoreSystem.csv     the system: instances + per-instance overrides
```

A system CSV overrides per instance: `cache_controller[*].num_mshr(i),16` applies to
every L1, `cache_controller[2].num_mshr(i),4` to one. Interfaces work the same way —
`configuration/Interconnect/SplitBusController.csv` extends `BusController.csv`, and
`bus[0].interconnect_controller.arbiter_type(s)` in the system file overrides it.

## 2. Typed keys

The suffix is the value's type: `(i)` int, `(s)` string, `(vi)` vector of ints,
`(vs)` vector of strings. This is what lets `-p` set *any* key without a schema:

```shell
-p "cache_controller[*].num_mshr(i)=4"
-p "bus_type(vs)=TripleBus,Bus"
```

Try it: find where the L1 MSHR depth is set, override it to 1 with `-p`, and watch the
L1 stall column in `Summary.csv` grow.

## 3. What is data and what is code

`MultiCoreSystem.csv` parameterises `src/SystemConfigurations/MultiCoreSystem.cpp`.
That C++ decides **what objects exist and how they are wired**: a loop creating one
L1 per core on `bus[0]`, one LLC between `bus[0]` and `bus[1]`, memory beyond. Every
other choice — protocol, arbiter, geometry, memory model — is data.

So `-s <name>` selects **both** a C++ class and its CSV, by the same name. You cannot
ship a `MyConfig.csv` and select it with `-s MyConfig` unless a `MyConfig` class
exists. That boundary — data below, wiring in C++ — is exactly what the
three-level-cache exercise in `03-extending-octopus/03-three-levels` crosses.

## 4. Presets

`configuration/SystemConfigurations/` also holds presets that are never selected
directly: `MultiCoreSystem_Snoop.csv`, `MultiCoreSystem_Directory.csv`.
`run_octopus.sh --protocol` *copies* the chosen one over `MultiCoreSystem.csv`. Look
at `git diff` after running one to see exactly which five keys a protocol is.

## 5. The two things the room will hit

- **Protocol is five coupled keys** (`cache_controller_type`, two `protocol_type`,
  two `fsm_filename`). Override some and the simulator faults or segfaults. Use the
  presets.
- The reset for everything in this exercise and the last one:
  `git checkout -- configuration/`.
