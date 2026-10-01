# Build the SLAM interference demo (slam_demo.cpp).
#
#   make               slam_demo: aarch64, static, with the m5ops (for gem5 SE)
#   make noroi         slam_demo-noroi: aarch64 without m5ops (qemu-user check)
#   make host          slam_demo-host: native build, to try it on this machine
#   make steps         viz/steps.json for the step-by-step figures and the plots
#
# Needs a gem5 checkout for the m5ops (GEM5_ROOT, or GEM5=...) and the aarch64
# cross compiler (g++-aarch64-linux-gnu). Static on purpose: no loader or
# sysroot is needed under gem5 SE.

GEM5     ?= $(GEM5_ROOT)
CROSS    ?= aarch64-linux-gnu-
# Plain '=': make predefines CXX, so '?=' would keep the host compiler.
CXX       = $(CROSS)g++
HOSTCXX  ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++20
ARCHFLAGS = -march=armv8-a
CPPFLAGS ?= -I$(GEM5)/include
LDFLAGS  ?= -pthread
M5OP_S    = $(GEM5)/util/m5/src/abi/arm64/m5op.S

.PHONY: all noroi host steps check-gem5 clean

all: slam_demo

check-gem5:
	@test -f "$(M5OP_S)" || { echo "set GEM5_ROOT (or GEM5=) to a gem5 checkout"; exit 1; }

m5op.o: | check-gem5
	$(CXX) $(CPPFLAGS) -c -o $@ $(M5OP_S)

slam_demo: slam_demo.cpp m5op.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ARCHFLAGS) -static -o $@ slam_demo.cpp m5op.o $(LDFLAGS)

noroi: slam_demo-noroi
slam_demo-noroi: slam_demo.cpp
	$(CXX) $(CXXFLAGS) $(ARCHFLAGS) -DNO_M5 -static -o $@ $< $(LDFLAGS)

host: slam_demo-host
slam_demo-host: slam_demo.cpp
	$(HOSTCXX) $(CXXFLAGS) -DNO_M5 -o $@ $< $(LDFLAGS)

steps: viz/steps.json
viz/dump_steps: viz/dump_steps.cpp slam_demo.cpp
	$(HOSTCXX) $(CXXFLAGS) -DNO_M5 -o $@ viz/dump_steps.cpp $(LDFLAGS)
viz/steps.json: viz/dump_steps
	cd viz && ./dump_steps

clean:
	rm -f slam_demo slam_demo-noroi slam_demo-host m5op.o viz/dump_steps viz/steps.json
