# Build the SE-mode Octopus test.  gem5 loads the binary straight from this
# directory (se_arm.py --binary): nothing is installed into the disk image and
# the image is not needed at all.
#
#   make                 cross-compile with $(CROSS)g++ (default aarch64-linux-gnu-)
#   make CROSS=          build natively on an aarch64 host
#   make run-qemu        smoke test under qemu-user (m5ops compiled out)
#
# Host packages: g++-aarch64-linux-gnu (pulls gcc-aarch64-linux-gnu,
# libstdc++-13-dev-arm64-cross and libc6-dev-arm64-cross) and, for run-qemu,
# qemu-user-static.
#
# The ROI markers (m5_work_begin / m5_work_end) come from gem5's own
# util/m5/src/abi/arm64/m5op.S, assembled here instead of linking a prebuilt
# libm5.a, which is not built for arm64 in this tree.

GEM5     ?= /home/guotong/gem5_arm
CROSS    ?= aarch64-linux-gnu-
# Plain '=': make predefines CXX, so '?=' would keep the host compiler.
CXX       = $(CROSS)g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17 -march=armv8-a
CPPFLAGS ?= -I$(GEM5)/include
LDFLAGS  ?= -pthread
SRC       = se_test.cpp
M5OP_S    = $(GEM5)/util/m5/src/abi/arm64/m5op.S
TARGETS   = se_test-static se_test-dynamic se_test-noroi
QEMU      = qemu-aarch64-static

.PHONY: all run-qemu clean

all: $(TARGETS)
	@file $(TARGETS)

m5op.o: $(M5OP_S)
	$(CXX) $(CPPFLAGS) -c -o $@ $<

# Static is what se_arm.py runs by default: no loader, no sysroot at run time.
se_test-static: $(SRC) m5op.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -static -o $@ $(SRC) m5op.o $(LDFLAGS)

# Dynamic needs se_arm.py --workload-type dynamic; the loader and libc come
# from the cross toolchain's sysroot (/usr/aarch64-linux-gnu) by default.
se_test-dynamic: $(SRC) m5op.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(SRC) m5op.o $(LDFLAGS)

# The m5op instructions are illegal outside gem5; this build stubs them so
# the workload itself can be checked under qemu-user.
se_test-noroi: $(SRC)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DNO_M5 -static -o $@ $(SRC) $(LDFLAGS)

run-qemu: se_test-noroi
	$(QEMU) ./se_test-noroi 2 256 200

clean:
	rm -f $(TARGETS) m5op.o
