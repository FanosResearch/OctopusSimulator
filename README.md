# Octopus
Octopus is a cycle-accurate cache system simulator with flexible interconnect models. It simulates various cache system and interconnect components, including controllers, data arrays, coherence protocols, and arbiters. Octopus enables the user to build reconfigurable simulation infrastructure for multicore processor chip with a high degree of flexibility of controlling system's configuration parameters. Octopus is implemented in C++ using object-oriented programming concepts to support a modular, expansible, configurable, and integrable design.

# Citation
If you use this simulator in your work, please consider cite:


Hossam, Mohamed, Salah Hessien, and Mohamed Hassan. "Octopus: a Cycle-Accurate Cache System Simulator." IEEE Computer Architecture Letters (2024). [Octopus](https://ieeexplore.ieee.org/iel8/10208/10700665/10633788.pdf?casa_token=2ABvIsydo2gAAAAA:hsgmaeaOe9CCwKII0mMr86OjOAPGSbHmI-9uq2-vg0GLbnT9YLhiS-nN1RYYT4d8jV2cmhsJQrs).

# Getting started
* The simulator is tested on both Linux Ubuntu 18.04.4 LTS and Ubuntu 20.04.01 releases. You may consider using Virtual Machine VM to install Ubuntu on your machine if it is not your primary operating system.  
* `$Octopus` refers to the top level directory where Octopus resides.
* Directory `$Octopus/src/` contains the source code of the simulator.
* Directory `$Octopus/header/` contains the header files.
* Directory `$Octopus/Protocols_FSM/` contains the CSV files that defines the coherency protocols' finite state machines.
* Directory `$Octopus/configuration/` contains the CSV files that contains the configuratable parameters of the simulation components.

## Building Octopus
Octopus uses CMake to manage the build system of the simulator. In order to build Octopus, you need to install the following:

```shell
sudo apt update
sudo apt upgrade
sudo apt-get install build-essential cmake
```

In order to build the simulator, we create a directory `$Octopus/build/`

```shell
mkdir $Octopus/build/
cd $Octopus/build/
cmake ../ .
make
```

Building for debug will require an extra flag to CMake

```shell
cd $Octopus/build/
cmake ../ . --DCMAKE_BUILD_TYPE=Debug
make
```

## Running Octopus

Running the simulator requires to choose a system configuration to run and a workload. In the example, we choose to run MultiCoreSystem with the workload TestBM in `$Octopus/BMs/TestBM/`. `$Octopus` should be replaced with the full path of the simulator's directory.

```shell
cd $Octopus/build/
./Octopus_Simulator -s MultiCoreSystem -p "workload_path(s)=$Octopus/BMs/TestBM/"
```
`-s` is used to specify the configuration, and `-p` is to overwrite any parameter in the configuration.

The default output reports will be found in `$Octopus/BMs/TestBM/newLogger/`.

## Intergrating Octopus with Gem5

### File structures
Ideally, we will have three folder under root /workspaces:
```
gem5/  -> Original Gem5
 ├─ src/
 ├─ ...
OctopusSimulator/ -> Octopus src
 ├─ header/
 ├─ MCSim/
 ├─ src/
 ├─ gem5/  -> Octopus Gem5 Interface
 | ├─ configs/ -> Octopus Gem5 sampel config
 | ├─ ...
 ├─ SConscript
 ├─ README.md
ATP-Engine/
 ├─ gem5/
 ├─ SConscript
 ├─ ...
```

### Build
Building MCSim
```shell
cd ${root}/OctopusSimulator/MCSim/src
make libmcsim.so
```

Building Octopus(CMSpec)
```shell
cd ${root}/OctopusSimulator/
mkdir build
cd build
cmake ../ .
make 
```

Build Gem5 + Octopus + ATP:
```shell
cd ${root}
git clone https://github.com/gem5/gem5.git # if you did not have this yet
cd ${root}/gem5
scons EXTRAS=../ATP-Engine:../OctopusSimulator -j $(nproc) build/ARM/gem5.fast
```

## How to run
Sample Arch config: 
1. configs/fs_arm.py
2. configs/octopus_cache_hierarchy.py

Sample run command:
```shell
export LD_LIBRARY_PATH=${root}/OctopusSimulator/build:${root}/OctopusSimulator/MCsim/src:$LD_LIBRARY_PATH
${root}/gem5/build/ARM/gem5.fast \
-d ${your_path_to_store_files} \
${root}/gem5/configs/fs_arm.py
```

### Run commands

Octopus hierarchy (preset `configuration/SystemConfigurations/MultiCoreSystem_gem5.csv`),
the classic-cache baseline built from the same preset, and parameter overrides:
```shell
cd ${root}/gem5
./build/ARM/gem5.opt -re -d <out> ${root}/OctopusSimulator/gem5/configs/fs_arm.py
./build/ARM/gem5.opt -re -d <out> ${root}/OctopusSimulator/gem5/configs/fs_arm.py --classic
./build/ARM/gem5.opt -re -d <out> ${root}/OctopusSimulator/gem5/configs/fs_arm.py \
    --octopus-param 'bus[0].interconnect_controller.m_response_latency(i)=1' \
    --octopus-param 'llc_controller.m_data_handler.m_data_array_pipelined(i)=0'
```
`--octopus-param` takes any `name(type)=value` line of the preset and may be
repeated. MESI instead of MSI on the snooping bus:
```
cache_controller_type(s)=CacheControllerExclusive
cache_controller[*].protocol_type(s)=SNOOP_MESI
cache_controller[*].fsm_filename(s)=MESI_splitBus_snooping
llc_controller.protocol_type(s)=SNOOP_LLC_MESI
llc_controller.fsm_filename(s)=MESI_LLC
```

Rebuilding after a change to the library: `cmake --build build` in
`${root}/OctopusSimulator`, then the `scons` line above (it relinks only the
bridge).

Standalone harnesses (single binaries against `build/libOctopus.so`, no gem5):
```shell
cd ${root}/OctopusSimulator
gem5/harness/build.sh
gem5/harness/l1 /tmp/octlog                       # hit and miss latencies, same-line bursts
gem5/harness/reorder /tmp/octlog "cache_controller[*].m_data_handler.m_data_access_latency(i)=10"
gem5/harness/stress /tmp/octlog --seed=2          # multi-core random contention, hang detector
```
Each takes a log directory and optional `name(type)=value` overrides. Debug
aids for any run: `OCTOPUS_TRACE_ADDR=<addr>` (or `1` for every line) prints
each controller's events on that line, `OCTOPUS_DUMP_AT=<cycle>` dumps every
controller's queues at that cycle.

The standalone lab presets are the regression check for library changes: run
`MultiCoreSystem_Directory` and `MultiCoreSystem_Snoop` as described at the
top of this file and diff `BMs/TestBM/newLogger/` against a build of the
commit you started from.

