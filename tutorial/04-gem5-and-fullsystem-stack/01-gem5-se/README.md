# 04/01 — gem5 + Octopus in SE mode

> Pending the gem5 environment. Steps below are the plan.

**Goal:** make the interface concrete — how a memory request leaves the gem5 CPU,
enters the Octopus hierarchy, and reaches the DRAM model — by running one short
SE-mode program and reading its `LatencyReport`. 15–20 minutes.

## The journey of a memory request

1. The gem5 CPU model issues a load or store.
2. It crosses into Octopus through `ExternalCPU` (`header/ExternalCPU.h`) — a
   `CommunicationInterface` producer exactly like the trace-driven `CPU` you used in
   exercises 00–03. Nothing downstream can tell the difference.
3. From there it is an ordinary Octopus `Message`: L1 controller, bus arbitration,
   LLC, and out to memory.
4. Memory is `MCsimInterface`: the address is handed to MCsim's DDR4 model and the
   fill comes back through a callback.
5. The `LatencyReport` row for that request decomposes exactly the path you just
   traced.

Steps 2–5 are **the same code** you ran standalone. Only the request source changed.

## Steps (to be finalised)

```shell
# prebuilt gem5 with Octopus linked in, provided by the container
$GEM5_ROOT/build/ARM/gem5.opt configs/se_octopus.py \
    --octopus-xml test/arm_challenge/<config>.xml --cmd <small SE binary>
```

Then compare the `LatencyReport` for the same configuration standalone versus under
gem5, and vary one thing in the XML — the bus arbiter is the natural choice — and
re-run. No rebuild is involved at any point.

## What you will *not* be able to do here

Change Octopus C++ and see it under gem5: that relinks `gem5.opt`. See the parent
README.
