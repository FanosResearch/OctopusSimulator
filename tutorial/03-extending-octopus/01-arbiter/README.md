# 03/01 — Add a bus arbiter

**Goal:** a new arbiter class, registered, compiled, selected from the command line,
and visible in the numbers. About 40 minutes.

**Suggested arbiter:** weighted round-robin where core 0 gets two slots per round.
It is about twenty lines against `RRArbiter`, and its effect shows immediately in
core 0's worst-case bus latency.

## The interface

An arbiter is a subclass of `Arbiter` (`header/Arbiters/Arbiter.h`) implementing one
pure virtual:

```cpp
virtual bool elect(uint64_t cycle_number,
                   vector<vector<Message> *> &buffers,   // one TX buffer per interface
                   Message *out_msg) = 0;                // the winner, if any
```

The Base `Arbiter` class also contains a useful helper function:
`electOldestOwned(buffers, owner, out)`,
which finds the oldest request currently waiting at the interconnect
destined for a given connected device (e.g. a cache controller)
whose ID is the `owner` parameter.

## Steps

1. Start by copying the round-robin arbiter as a base:
   ```shell
   cp header/Arbiters/RRArbiter.h  header/Arbiters/WRRArbiter.h
   cp src/Arbiters/RRArbiter.cpp   src/Arbiters/WRRArbiter.cpp
   ```
   Substitute `WRR` for `RR` in the header guard and code to
   prevent conflicts:
   ```shell
   sed -i 's/RRArbiter/WRRArbiter/g' header/Arbiters/WRRArbiter.h src/Arbiters/WRRArbiter.cpp
   sed -i 's/_RR_ARBITER_H/_WRR_ARBITER_H/g' header/Arbiters/WRRArbiter.h
   ```

2. Add `WRRArbiter.cpp` to `src/Arbiters/CMakeLists.txt`.

3. Include the new class in `src/Interconnect/SplitBusController.cpp`:

   ```cpp
   #include "../../header/Arbiters/WRRArbiter.h"
   ```

4. Be sure to register the name in `src/Interconnect/SplitBusController.cpp`, adding a new case in the
   `if (arbiter_type == STRINGIFY(...))` chain for `WRRArbiter`. 

5. Rebuild and verify the unchanged copy against RR. Run from the project root:

   ```shell
   cmake --build build -j$(nproc)
   W=$PWD/BMs/eembc-traces/cacheb01-trace
   # Windows/Git Bash: W=$(cygpath -m "$PWD/BMs/eembc-traces/cacheb01-trace")
   OUT=tutorial/03-extending-octopus/01-arbiter/output

   # Collect the results from the original round robin arbiter
   ./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
       -p "bus[0].interconnect_controller.arbiter_type(s)=RRArbiter" \
       -o "$OUT/RR" --trace

   # Collect the results from our clean copy, to be modified later
   ./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
       -p "bus[0].interconnect_controller.arbiter_type(s)=WRRArbiter" \
       -o "$OUT/WRR" --trace

   # Verify that they are the same
   diff -r "$OUT/RR" "$OUT/WRR"
   ```

   With an unmodified copy of RR, the CSV reports should be identical: `diff`
   should print nothing and exit. This verifies that you have a clean, working
   base from which to add weights to the basic round-robin algorithm.

6. Implement the weighting in `WRRArbiter` (see the hint below), then rebuild and repeat only the WRR
   run, using the same output path:

   ```shell
   cmake --build build -j$(nproc)
   ./build/Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$W/" \
       -p "bus[0].interconnect_controller.arbiter_type(s)=WRRArbiter" \
       -o "$OUT/WRR" --trace

   diff -u "$OUT/RR/Summary_transposed.csv" "$OUT/WRR/Summary_transposed.csv"
   ```

   This overwrites the initial WRR reports with your weighted version while keeping
   RR as the reference. We should now expect WRR to have different performance metrics. 
   
**Hint**: the RRArbiter code uses a modulo counter in function 
`selectCandidate` to select each of the connected controllers in
succession. You want instead for Core 0 to be picked twice in
each round of this process. You may also need to modify `elect`.

  Compare the two `Summary_transposed.csv` files to see how Core 0's performance
  changes, now that WRR gives it extra service.  

   Optionally plot the saved runs:

   ```shell
   python3 sweeps/plot_axis.py "$OUT"
   ```

   The plots aggregate across cores; use the summaries for the per-core effect of
   weighting. Figures are saved under `output/figures/`, outside the setting
   directories.
   Also try using the visualizer to inspect the two simulations:
   ```shell
# Refresh WRR after overwriting its run; serve reuses any existing Parquet files.
KEEP_TRACE=1 ./octoviz.sh convert "$OUT/WRR"
./octoviz.sh serve "$OUT"
```
   Can you see any difference in how the messages are serviced,
   depending on their issuing cores?

## Stretch

Make the Core 0 weight configurable through `bus[0].interconnect_controller.wrr_weight(i)`,
supplied using `-p`.

You will need to have the `WRRArbiter` constructor take in the value
as an argument, and have the `SplitBusController` class supply it
by reading from the CSV parameter map (see how the `SplitBusController`
class reads parameter `arbiter_type` in its constructor for reference).

Interpret the value as the total weight: 1 gives ordinary round-robin, and 2 gives
Core 0 two slots per round. Default to 2 if omitted, and reject values below 1.
