# m5ops for aarch64 workloads

The pieces of gem5's m5ops that the SE workloads in this repository need
(`gem5/se_test`),
copied unchanged from gem5 v25.1.0.1 so those programs build without a gem5
checkout:

| file | from gem5 |
| --- | --- |
| `include/gem5/m5ops.h` | `include/gem5/m5ops.h` |
| `include/gem5/asm/generic/m5ops.h` | `include/gem5/asm/generic/m5ops.h` |
| `m5op.S` | `util/m5/src/abi/arm64/m5op.S` |

The Makefiles assemble `m5op.S` into `m5op.o` and link it in, instead of a
prebuilt `libm5.a`. Each file keeps its original copyright and license header.
To use another gem5's copies, pass `M5OPS=<dir>` with the same layout, or
`GEM5=<gem5 checkout>`.
