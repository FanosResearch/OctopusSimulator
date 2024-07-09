#!/bin/bash

simulate()
{
    echo "******************************** ${1} ********************************"     
    # grep simInsts m5out/$1.txt   
    
    # grep board.processor.cores0.core.numLoad m5out/$1.txt
    # grep board.processor.cores0.core.numStoreInsts m5out/$1.txt
    # grep board.processor.cores1.core.numLoad m5out/$1.txt
    # grep board.processor.cores1.core.numStoreInsts m5out/$1.txt
    # grep board.processor.cores2.core.numLoad m5out/$1.txt
    # grep board.processor.cores2.core.numStoreInsts m5out/$1.txt
    # grep board.processor.cores3.core.numLoad m5out/$1.txt
    # grep board.processor.cores3.core.numStoreInsts m5out/$1.txt

    grep board.processor.cores0.core.numCycles m5out/$1.txt
    grep board.processor.cores1.core.numCycles m5out/$1.txt
    grep board.processor.cores2.core.numCycles m5out/$1.txt
    grep board.processor.cores3.core.numCycles m5out/$1.txt

    grep board.processor.cores0.core.idleCycles m5out/$1.txt
    grep board.processor.cores1.core.idleCycles m5out/$1.txt
    grep board.processor.cores2.core.idleCycles m5out/$1.txt
    grep board.processor.cores3.core.idleCycles m5out/$1.txt
}

# simulate RADIX
# simulate LU
# simulate FFT
# simulate BARNS
# simulate FMM
# simulate RADIOSITY
simulate WATER

