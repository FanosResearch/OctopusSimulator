# 04 — gem5 and the full-system stack

> **Status: environment pending.** The gem5 build is not in the dev container yet.
> These two exercises are written against `gem5/cmds.md` on the
> `gem5_ARM_Challenge` branch and will be finalised once the container carries a
> prebuilt `gem5.opt`. Until then, treat the steps as the plan, not the procedure.

Everything so far ran Octopus **standalone**: a trace-driven `CPU` object feeds the
memory hierarchy. Here the request source is a real CPU model — gem5 — and the same
hierarchy, unchanged, sits behind it.

| folder | what | time |
|---|---|---|
| `01-gem5-se/` | gem5 in syscall-emulation mode driving Octopus; the journey of one memory request | 15–20 min |
| `02-full-system-arm/` | ARM Linux under gem5, restored from a checkpoint, with Octopus as its memory system | 25–30 min + take-home |

## The one constraint to know before starting

Octopus is compiled **into** gem5 as a scons `EXTRAS` module:

```shell
scons EXTRAS=../ATP-Engine:../OctopusSimulator ./build/ARM/gem5.opt -j`nproc`
```

So an Octopus C++ edit means relinking `gem5.opt` — minutes, not the 50-second
rebuild you had in exercise 03. Both exercises here vary **configuration** (the
`--octopus-xml` file, gem5's own options), never Octopus source. The arbiter you
wrote in `03-extending-octopus/01-arbiter` will not appear inside gem5 unless the
container's `gem5.opt` was built with it.
