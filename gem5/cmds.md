apptainer shell -C \
-B ./gem5:/workspaces/gem5 \
-B ./gem5_resource:/workspaces/gem5/resource \
-B ./OctopusSimulator:/workspaces/OctopusSimulator \
-B ./ATP-Engine:/workspaces/ATP-Engine \
gem5-latest.sif

cmake -DCMAKE_BUILD_TYPE=Debug .. .

scons EXTRAS=../ATP-Engine:../OctopusSimulator ./build/ARM/gem5.opt -j`nproc`

export LD_LIBRARY_PATH=/workspaces/OctopusSimulator/build:/workspaces/OctopusSimulator/MCsim/src:$LD_LIBRARY_PATH

/workspaces/gem5/build/ARM/gem5.opt -re --debug-flags=Octopus --debug-file=trace.out -d /workspaces/OctopusSimulator/log/bnkpart /workspaces/OctopusSimulator/gem5/configs/fs_arm.py --octopus-xml /workspaces/OctopusSimulator/test/arm_challenge/tc_FR_10C_8B_bnpart.xml


apptainer shell -C \
-B ./gem5:/workspaces/gem5 \
-B ./disk-image:/workspaces/gem5/resource \
-B ./OctopusSimulator:/workspaces/OctopusSimulator \
gem5-latest.sif

scons EXTRAS=../OctopusSimulator ./build/ARM/gem5.opt -j`nproc`

salloc --time=1:0:0 --cpus-per-task=32 --mem=128G

tee /ov2slam/ov2slam_launch.py > /dev/null << 'PYEOF'
from launch import LaunchDescription
from launch.actions import ExecuteProcess, Shutdown
from launch_ros.actions import Node

def generate_launch_description():
    node = Node(
        package='ov2slam',
        executable='ov2slam_node',
        name='ov2slam_node',
        output='screen',
        arguments=['/ov2slam/euroc_stereo.yaml'],
        prefix=['taskset -c 0,1'],
    )
    bag = ExecuteProcess(
        cmd=['ros2', 'bag', 'play', '/ov2slam/1sout_humble'],
        output='screen',
        prefix=['taskset -c 2'],
        on_exit=Shutdown(),
    )
    return LaunchDescription([node, bag])
PYEOF

rosbags-convert \
  --src /ov2slam/1sout.bag \
  --dst /ov2slam/1sout_humble \
  --dst-version 8 \
  --dst-typestore ros2_humble \
  --exclude-topic /imu0 \
  --exclude-topic /leica/position
  

16691919 gem5_oct_o3_ov2_2204_cp_v2
17579602 gem5_ov2_o3_classic_3s_cp
