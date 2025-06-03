apptainer shell -C \
-B ./gem5:/workspaces/gem5 \
-B ./gem5_resource:/workspaces/gem5/resource \
-B ./OctopusSimulator:/workspaces/OctopusSimulator \
-B ./ATP-Engine:/workspaces/ATP-Engine \
gem5-latest.sif

cmake -DCMAKE_BUILD_TYPE=Debug .. .

scons EXTRAS=../ATP-Engine:../OctopusSimulator ./build/ARM/gem5.opt -j`nproc`

export LD_LIBRARY_PATH=/workspaces/OctopusSimulator/build:/workspaces/OctopusSimulator/MCsim/src:$LD_LIBRARY_PATH

/workspaces/gem5/build/ARM/gem5.opt -re --debug-flags=Octopus --debug-file=trace.out -d /workspaces/OctopusSimulator/log/bnpart /workspaces/OctopusSimulator/gem5/configs/fs_arm.py --octopus-xml /workspaces/OctopusSimulator/test/arm_challenge/tc_FR_10C_8B_bnpart.xml