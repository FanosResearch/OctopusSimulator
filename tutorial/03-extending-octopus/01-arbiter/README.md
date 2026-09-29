# 03/01 — Add a bus arbiter

**Goal:** a new arbiter class, registered, compiled, selected from the command line,
and visible in the numbers. About 40 minutes.

**Suggested arbiter:** weighted round-robin where core 0 gets two slots per round.
It is about twenty lines against `RRArbiter`, and its effect shows immediately in
core 0's worst-case bus latency — which is also the story the papers tell.

## The interface

An arbiter is a subclass of `Arbiter` (`header/Arbiters/Arbiter.h`) implementing one
pure virtual:

```cpp
virtual bool elect(uint64_t cycle_number,
                   vector<vector<Message> *> &buffers,   // one TX buffer per interface
                   Message *out_msg) = 0;                // the winner, if any
```

Base-class helpers you will want: `findMessage()`, and `electOldestOwned(buffers,
owner, out)` — pick the oldest message owned by a given core across *all* buffers.
(That helper exists because the naive "first buffer with a matching owner" starved
cache-to-cache supplies; use it rather than re-deriving it.)

## Steps

1. Start from the round-robin one:

   ```shell
   cp header/Arbiters/RRArbiter.h  header/Arbiters/WRRArbiter.h
   cp src/Arbiters/RRArbiter.cpp   src/Arbiters/WRRArbiter.cpp
   sed -i 's/RRArbiter/WRRArbiter/g' header/Arbiters/WRRArbiter.h src/Arbiters/WRRArbiter.cpp
   ```

2. Add `WRRArbiter.cpp` to `src/Arbiters/CMakeLists.txt`.

3. **Register the name** in `src/Interconnect/SplitBusController.cpp`, in the
   `if (arbiter_type == STRINGIFY(...))` chain. This is the step people miss — see
   the trap below.

4. Rebuild, then select it:

   ```shell
   cmake --build build -j$(nproc)
   W=$PWD/BMs/eembc-traces/cacheb01-trace
   ./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
       -p "bus[0].interconnect_controller.arbiter_type(s)=WRRArbiter"
   column -s, -t $W/newLogger/Summary.csv | less -S
   ```

   With an unmodified copy of RR you must get **exactly** RR's numbers — that is your
   check that the registration works. Then make it weighted.

5. Compare core 0's "Worst-case Requst Bus Latency" against `RRArbiter` on
   `cacheb01` (four cores fighting over one block — the arbiter matters most there).

## The trap

The tutorial's `bus[0]` is a `TripleBus` whose `controller_type` is `Split`, so
`SplitBusController.cpp` is the file that decides which arbiters exist on the bus you
are measuring. `MeshController.cpp` and `NoCController.cpp` have the same chain for
the mesh — registering there instead compiles cleanly and does nothing.

## Stretch

Make the weight a parameter: read `bus[0].interconnect_controller.wrr_weight(vi)` in
the constructor and select it with `-p`. Look at how `TDMArbiter` reads its slot table
for the pattern.
