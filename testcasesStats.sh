#!/bin/bash
num_cores=4
config_path="/home/gem5/gem5_new/ext/CMSpec/CMSpec/test/myTests/tc1_4.xml"
cache=Octopus #Octopus #Ruby #NoCache

simulate()
{
    ./build/X86/gem5.debug /home/gem5/gem5_new/configs/mytest2/hello.py -f $config_path -c $num_cores -b $1 -m $cache     
    
    grep simInsts m5out/stats.txt   
    
    grep board.processor.cores0.core.exec_context.thread_0.numLoad m5out/stats.txt
    grep board.processor.cores0.core.exec_context.thread_0.numStoreInsts m5out/stats.txt
    grep board.processor.cores0.core.exec_context.thread_0.numIdleCycles m5out/stats.txt
    grep board.processor.cores0.core.exec_context.thread_0.numBusyCycles m5out/stats.txt
    grep board.processor.cores1.core.exec_context.thread_0.numLoad m5out/stats.txt
    grep board.processor.cores1.core.exec_context.thread_0.numStoreInsts m5out/stats.txt
    grep board.processor.cores1.core.exec_context.thread_0.numIdleCycles m5out/stats.txt
    grep board.processor.cores1.core.exec_context.thread_0.numBusyCycles m5out/stats.txt
    grep board.processor.cores2.core.exec_context.thread_0.numLoad m5out/stats.txt
    grep board.processor.cores2.core.exec_context.thread_0.numStoreInsts m5out/stats.txt
    grep board.processor.cores2.core.exec_context.thread_0.numIdleCycles m5out/stats.txt
    grep board.processor.cores2.core.exec_context.thread_0.numBusyCycles m5out/stats.txt
    grep board.processor.cores3.core.exec_context.thread_0.numLoad m5out/stats.txt
    grep board.processor.cores3.core.exec_context.thread_0.numStoreInsts m5out/stats.txt
    grep board.processor.cores3.core.exec_context.thread_0.numIdleCycles m5out/stats.txt
    grep board.processor.cores3.core.exec_context.thread_0.numBusyCycles m5out/stats.txt
    
    mv m5out/stats.txt m5out/$1.txt

    echo "*********************************** Core 0 ***********************************"
    grep "Num of Requests"      .vscode/core0.txt | tail -1
    grep "Num of Hits"          .vscode/core0.txt | tail -1
    grep "Num of Misses"        .vscode/core0.txt | tail -1
    grep "Num of Replacments"   .vscode/core0.txt | tail -1
    grep "Num of Interference"  .vscode/core0.txt | tail -1
    echo "*********************************** Core 1 ***********************************"
    grep "Num of Requests"      .vscode/core1.txt | tail -1
    grep "Num of Hits"          .vscode/core1.txt | tail -1
    grep "Num of Misses"        .vscode/core1.txt | tail -1
    grep "Num of Replacments"   .vscode/core1.txt | tail -1
    grep "Num of Interference"  .vscode/core1.txt | tail -1
    echo "*********************************** Core 2 ***********************************"
    grep "Num of Requests"      .vscode/core2.txt | tail -1
    grep "Num of Hits"          .vscode/core2.txt | tail -1
    grep "Num of Misses"        .vscode/core2.txt | tail -1
    grep "Num of Replacments"   .vscode/core2.txt | tail -1
    grep "Num of Interference"  .vscode/core2.txt | tail -1
    echo "*********************************** Core 3 ***********************************"
    grep "Num of Requests"      .vscode/core3.txt | tail -1
    grep "Num of Hits"          .vscode/core3.txt | tail -1
    grep "Num of Misses"        .vscode/core3.txt | tail -1
    grep "Num of Replacments"   .vscode/core3.txt | tail -1
    grep "Num of Interference"  .vscode/core3.txt | tail -1
}

simulate RADIX
simulate LU
simulate FFT
simulate BARNS
simulate FMM
simulate RADIOSITY
simulate WATER

