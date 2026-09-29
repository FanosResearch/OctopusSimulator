# 03/03 — Three cache levels instead of two

**Goal:** see the boundary between data and code, and cross it: a new *system*
configuration with an L2 between the L1s and the LLC. Driven from the front; follow
along, or try it yourself if the previous two felt easy. About 45 minutes.

## Why this is code, not configuration

`src/SystemConfigurations/MultiCoreSystem.cpp` hardwires the shape — a loop creating
one L1 per core on `bus[0]`, one `llc_controller` between `bus[0]` and `bus[1]`,
memory beyond. There is no parameter for a third level; the wiring is in C++. So this
exercise is a new class beside `MultiCoreSystem` and `MultiCoreSystem_Mesh`, selected
with `-s ThreeLevelSystem`, which needs `ThreeLevelSystem.{h,cpp}` **and**
`configuration/SystemConfigurations/ThreeLevelSystem.csv` (same name — that is how
`-s` finds both).

## Why it is only 45 minutes

Every building block already exists and is generic:

| block | what it gives you |
|---|---|
| `createController(type, map, lower_iface, upper_iface, name)` | a controller of any type from a parameter sub-map |
| `Bus` / `TripleBus` | a bus from its own sub-map (`getSubMap("bus", i)`) |
| `bus_type(vs)` | already sizes the bus array from the CSV — a third bus is a CSV line |
| `getSubMap("cache_controller", i)` | per-instance parameters |

The new class is `MultiCoreSystem.cpp` with one more loop: L1s on `bus[0]`, private
L2s between `bus[0]` and `bus[1]`, the LLC between `bus[1]` and `bus[2]`, memory
on `bus[2]`. Register the class where `MultiCoreSystem_Mesh` is registered (grep for
it in `src/`), add the source to the `SystemConfigurations` CMake list.

## What to look at when it runs

Run `a2time01` on both systems and compare `Summary.csv`. The "L2 Stall" and "L2
Access" columns now mean the private L2; the LLC's stages are folded into the
bus/DRAM columns — a good moment to ask how the Logger's stage attribution should be
extended (it is tagged per controller with `setLogRole`; see how `MultiCoreSystem`
tags the LLC).

## The open question you will hit

Coherence. A private L2 between a snooping L1 and the LLC is not a transparent
box: something has to snoop on behalf of the L1. The cheapest correct answer for
the exercise is an *inclusive* L2 that forwards every bus transaction up — which is
what the reference solution does — and the discussion of what a real design would do
instead is the point of the segment.
