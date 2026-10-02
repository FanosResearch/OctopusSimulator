# The tutorial's dev container image, ghcr.io/fanosresearch/esweek.
#
# Built by build.sh, which puts the binary inputs into ctx/ first (gem5, the ARM
# kernel and bootloader, the full-system disk image and checkpoints). One layer per
# kind of content, so a pull fetches them in parallel and a change to one (a new
# gem5) re-downloads only that layer. The disk image ships compressed: unpacked it
# is 9 GB, and only exercise 04/02 uses it (gem5/get_disk_image.sh unpacks it).
FROM mcr.microsoft.com/devcontainers/cpp:1.2.14-noble

# Build tools, the aarch64 cross compiler for the SE workloads, and the libraries
# gem5 links (protobuf, tcmalloc, libpython3.12), zstd for the full-system disk image;
# the last line upgrades packages the base image already has.
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        device-tree-compiler g++-aarch64-linux-gnu gcc-aarch64-linux-gnu \
        libc6-arm64-cross libc6-dev-arm64-cross libgoogle-perftools-dev \
        libprotobuf-dev m4 protobuf-compiler python-is-python3 python3-dev \
        python3-pip scons software-properties-common tmux zstd \
        ca-certificates gdb zlib1g zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

# Python for the analysis scripts and plots (scripts/check_environment.sh checks these).
RUN pip install --no-cache-dir --break-system-packages \
        numpy==2.5.3 matplotlib==3.11.2 duckdb==1.5.6 \
        contourpy==1.4.0 cycler==0.12.1 fonttools==4.66.1 kiwisolver==1.5.1 \
        packaging==26.3 pillow==12.3.0 python-dateutil==2.9.0.post0

# gem5 v25.1.0.1 for ARM with this repository as EXTRAS (gem5/get_gem5.sh). It loads
# libOctopus.so through its RUNPATH, /workspaces/OctopusSimulator/build: the library
# the container builds from the checkout.
COPY ctx/gem5 /usr/local/bin/gem5

# Kernel and bootloader for full system (gem5/configs/fs_arm.py).
COPY --chown=vscode:vscode ctx/gem5-resources/ /opt/gem5-resources/

# Full system, exercise 04/02: the disk image, compressed, and the ROI checkpoints
# of the ov2slam clips (ov2_start_trim_<clip>, gem5/configs/ov2slam/checkpoints.py).
COPY --chown=vscode:vscode ctx/fs/ /opt/gem5-resources/
RUN chmod 777 /opt/gem5-resources

# libOctopus.so and libmcsim.so for anything else run from the checkout.
ENV LD_LIBRARY_PATH=/workspaces/OctopusSimulator/build:/workspaces/OctopusSimulator/src/MCsim/src
RUN line='export LD_LIBRARY_PATH="/workspaces/OctopusSimulator/build:/workspaces/OctopusSimulator/src/MCsim/src${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"' \
 && echo "$line" > /etc/profile.d/octopus.sh \
 && echo "$line" >> /etc/bash.bashrc

USER vscode
WORKDIR /workspaces
