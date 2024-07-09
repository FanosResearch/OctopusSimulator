#!/bin/bash

for i in 2 4 8 16 32
do
  num_cores=$i 
  config_path="/home/gem5/gem5_new/ext/CMSpec/CMSpec/test/myTests/tc1_l1l2_llc_"
  config_path="${config_path}${num_cores}.xml"

  echo $num_cores
  echo $config_path
  /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b RADIX -m Octopus

  grep simTicks m5out/stats.txt
  grep finalTick m5out/stats.txt
  grep board.clk_domain.clock m5out/stats.txt
done

# for i in 2 4 8 16 32
# do
#   num_cores=$i 
#   echo $num_cores
#   config_path="/home/gem5/gem5_new/ext/CMSpec/CMSpec/test/myTests/tc1.xml"
#   /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b RADIX -m Octopus

#   grep simTicks m5out/stats.txt
#   grep finalTick m5out/stats.txt
#   grep board.clk_domain.clock m5out/stats.txt
# done

# for i in 2 4 8 16 32
# do
#   num_cores=$i 
#   echo $num_cores
#   /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RADIX -m Ruby

#   grep simTicks m5out/stats.txt
#   grep finalTick m5out/stats.txt
#   grep board.clk_domain.clock m5out/stats.txt
# done