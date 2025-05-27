# CacheSim
CacheSim is a trace-base cycle-accurate simulator for cache-coherent interconnect architectures. CacheSim enables the user to build reconfigurable simulation infrastructure for multi-core processor chip with a high degree of flexibility of controlling system's configuration parameters such as cache organization, coherence protocol models, and interconnect bus architecture as well as bus arbitration policies. CacheSim is implemented in C++ using object-oriented programming concepts to support a modular, expansible, configurable, and integrable design.

# Getting started
* The simulator is tested on both Linux Ubuntu 18.04.4 LTS and Ubuntu 20.04.01 releases. You may consider using Virtual Machine VM to install Ubuntu on your machine if it is not your primary operating system.  
* `$CacheSim` refers to the top level directory where CacheSim resides.
* `CacheSim/scratch/MultiCoreSimulator.cc` is the main function of the simulator.  
* Directory `$CacheSim/src/MultiCoreSim` contains source code and test cases for building and running CacheSim.
* CacheSim accepts test-case configuration parameters in XML format, `$CacheSim/src/MultiCoreSim/test` directory contains examples of various experiments that can be used to run the simulator.

## Building CacheSim
CacheSim uses [WAF building automation tool](https://waf.io/) to configure, build, and run simulator. In order to build CacheSim, you need to install the following packages.

```shell
sudo apt update
sudo apt upgrade
sudo apt-get install build-essential autoconf automake libxmu-dev python
```
you also need to install Lcov tool in order to run the simulator in the code coverage mode,

```shel
sudo apt-get install lcov
```

Waf tool allows the user to configure and run CacheSim in three different operation modes as follows.

* **Experimental mode** is the fastest simulation mode where all debug messages and coverage report features are disabled. This mode is used during architectural explorations once the developed feature is stable, and the main focus is on evaluating developed algorithms' performance. The following command is used to run the simulator in experiemental mode.

```shell
cd $CacheSim
./waf --run "scratch/MultiCoreSimulator --CfgFile=./src/MultiCoreSim/test/piscot/piscot_tc17.xml --BMsPath=../../../BMs/eembc-traces/iirflt01-trace"
```
The command line option `--CfgFile` used to configure the testcase file name, and `--BMsPath` use to configure the path of the memory request trace files. The trace based simulation reads from a file `trace_C<n>.trc.shared` that consists of lines of requests of the form `Addr OP time`. OP is of type RD for read and WR for write, and time is a positive integer value that denotes the arrival time of the memory request to the memory hierarchy. The parameter `n` indicates the processing core index that use to process this trace file.

* **Debug mode** allows the user to dump the internal states of the simulator every clock cycle. For instance, dump the internal states of cache coherence protocols and handshaking signals between processor controllers and upper-level controllers (i.e. shared memory controllers). To enable the debug mode, you need to enable `--LogFileGenEnable=1` flag as 

```shell
./waf --run "scratch/MultiCoreSimulator --CfgFile=./src/MultiCoreSim/test/piscot/piscot_tc17.xml --BMsPath=../../../BMs/eembc-traces/iirflt01-trace --LogFileGenEnable=1"
```

For advanced debugging, the following command can be used to attach `gdb` into CacheSim to allow the user see what is going on inside the C++ code, and run the program line by line in addition to set breakpoints.

```shell
./waf --run scratch/MultiCoreSimulator --command-template="gdb --args %s --CfgFile=./src/MultiCoreSim/test/piscot/piscot_tc19.xml --BMsPath=../../../BMs/synth/L2Replc6"
 ```

* **Coverage mode** provides information on the execution of the code line by line in terms of line and function coverage. To run the simulator with code coverage. To run code coverage, you need to add this option `--enable-gcov` to the waf command

```shell
./waf --enable-gcov --run "scratch/MultiCoreSimulator --CfgFile=./src/MultiCoreSim/test/piscot/piscot_tc17.xml --BMsPath=../../../BMs/eembc-traces/iirflt01-trace --LogFileGenEnable=1"
```
Then, run the following commands to generate `html` coverage analysis reports.
```shell
lcov --capture --directory . --output-file coverage.rpt
genhtml coverage.rpt --output-directory covOut
```

