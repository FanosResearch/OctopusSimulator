# 03 — Extending Octopus

Two exercises of different natures. Pick **one** — allow 45 minutes to complete it,
then compare notes at the end.

| folder | what you add | nature | rebuild |
|---|---|---|---|
| `01-arbiter/` | a new bus arbiter | C++, small, well-fenced | ~50 s on a codespace |
| `02-protocol/` | a state or event in a coherence protocol | CSV only | none |

Both edit the **real source tree** — `src/Arbiters/` and `Protocols_FSM/`. The exercise folders hold only the
instructions. You are learning the repository as it is.

The rebuild after touching one file:

```shell
cmake --build build -j$(nproc)
```
