# Publications Built on Octopus

Octopus has served as the modeling and evaluation platform for a growing body of
peer-reviewed research in real-time and embedded memory systems. The project has
been under active development since 2018, and this list is **maintained
continuously** — to add a paper, open a pull request or an issue.

## The simulator

- **Octopus: A Cycle-Accurate Cache System Simulator.**
  M. Hossam, S. Hessien, M. Hassan.
  *IEEE Computer Architecture Letters (CAL)*, 2024.
- **Octopus: gem5-Integrated Rapid Prototyping for Resource-Contention Measurement
  and Control** — full-system Arm/Linux extension.
  Y. Lai, G. Miao, S. Abdelhalim, M. Hossam, Y. Chen, R. Pellizzoni, M. Hassan.
  *ECRTS Industrial Challenge (workshop)*, 2025.

## Works evaluated on Octopus

These works used Octopus to produce their results.

- **DISCO — Discriminative Coherence: Balancing Performance and Latency Bounds in
  Data-Sharing Multi-Core Real-Time Systems.**
  M. Hassan. *ECRTS*, 2020. *(extended journal version in IEEE Transactions on
  Computers).*
- **PISCOT — The Best of All Worlds: Improving Predictability at the Performance of
  Conventional Coherence with No Protocol Modifications.**
  S. Hessien, M. Hassan. *IEEE RTSS*, 2020. *(extended journal version in ACM
  Transactions on Embedded Computing Systems, TECS).*
- **Duetto — Latency Guarantees at Minimal Performance Cost.**
  R. Mirosanlou, M. Hassan, R. Pellizzoni. *DATE*, 2021.
- **DUPECO — Parallelism-Aware High-Performance Cache Coherence with Tight Latency
  Bounds.**
  R. Mirosanlou, M. Hassan, R. Pellizzoni. *ECRTS*, 2022.
- **PCC — Predictably and Efficiently Integrating COTS Cache Coherence in Real-Time
  Systems.**
  M. Hossam, M. Hassan. *ECRTS*, 2022.
- **GRROF — A Tight Holistic Memory Latency Bound Through Coordinated Management of
  Memory Resources.**
  S. Abdelhalim, D. Germchi, M. Hossam, R. Pellizzoni, M. Hassan. *ECRTS*, 2023.
- **CoHoRT — Criticality- and Requirement-Aware Heterogeneous Coherence for
  Mixed-Criticality Systems.**
  S. Bayes, M. Hassan. *DATE*, 2025.

## Predictable-coherence protocols re-implemented in Octopus

These protocols were originally evaluated on other infrastructure and were later
added to Octopus, so they can be studied and compared head-to-head under
identical conditions — a direct demonstration of Octopus's extensibility.

- **PMSI** — predictable MSI.
  A. M. Kaushik, M. Hassan, H. Patel. *IEEE Transactions on Computers*, 2021.
- **PMESI** — predictable MESI.
- **PENDULUM** — Enabling Predictable, Simultaneous, and Coherent Data Sharing in
  Mixed-Criticality Systems.
  N. Sritharan, A. Kaushik, M. Hassan, H. Patel. *IEEE RTSS*, 2019.
- **PMSI\*** — enhanced predictable MSI.
- **PMESI\*** — enhanced predictable MESI.

---

*Using Octopus in your work? We would be glad to add it — open a pull request
against this file.*
