# 03 — Extending Octopus

Three exercises of different natures. Pick **one** — 45 minutes is enough for one done
properly, not three — and we compare notes at the end.

| folder | what you add | nature | rebuild |
|---|---|---|---|
| `01-arbiter/` | a new bus arbiter | C++, small, well-fenced | ~50 s on a codespace |
| `02-protocol/` | a state or event in a coherence protocol | CSV only | none |
| `03-three-levels/` | a third cache level | a new system class | ~50 s |

All three edit the **real source tree** — `src/Arbiters/`, `Protocols_FSM/`,
`src/SystemConfigurations/` — not a copy. The exercise folders hold only the
instructions and the answer key. You are learning the repository as it is.

The rebuild after touching one file:

```shell
cmake --build build -j$(nproc)
```

If you get stuck, `bash tutorial/solutions/apply.sh <folder-name>` applies the
reference solution so you can move on.
