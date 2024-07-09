#!/bin/bash
num_cores=4
config_path="/home/gem5/gem5_new/ext/CMSpec/CMSpec/test/myTests/tc1_4.xml"
               
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RADIX -m NoCache     
grep simInsts m5out/stats.txt
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b FFT -m NoCache    
grep simInsts m5out/stats.txt
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b LU  -m NoCache         
grep simInsts m5out/stats.txt       
# ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b CHOLESKY    
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b BARNS -m NoCache      
grep simInsts m5out/stats.txt  
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b FMM -m NoCache        
grep simInsts m5out/stats.txt 
# ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b OCEAN           
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RADIOSITY -m NoCache
grep simInsts m5out/stats.txt
# ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RAYTRACE        
./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b WATER -m NoCache       
grep simInsts m5out/stats.txt
# ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b Threads



# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b RADIX     -m Octopus
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b FFT       -m Octopus
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b LU        -m Octopus

# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b BARNS     -m Octopus
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b FMM       -m Octopus
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b RADIOSITY -m Octopus
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b WATER     -m Octopus


# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RADIX     -m Ruby
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b FFT       -m Ruby
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b LU        -m Ruby

# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b BARNS     -m Ruby
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b FMM       -m Ruby
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b RADIOSITY -m Ruby
# /usr/bin/time -v ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -c $num_cores -b WATER     -m Ruby
