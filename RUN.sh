#!/bin/bash
echo "PID is $$"
# trap 'kill $(jobs -p)' EXIT

# source test.sh MSI 2 > terminal_MSI.out 2>&1 &
# source test.sh MESI 2 > terminal_MESI.out 2>&1 &
# source test.sh MOESI 2 > terminal_MOESI.out 2>&1 &
# # wait

# source test.sh MSI 4 > terminal_MSI2.out 2>&1 &
# source test.sh MESI 4 > terminal_MESI2.out 2>&1 &
# source test.sh MOESI 4 > terminal_MOESI2.out 2>&1 &
# # wait

# source test.sh MSI 8 > terminal_MSI3.out 2>&1 &
# source test.sh MESI 8 > terminal_MESI3.out 2>&1 &
# source test.sh MOESI 8 > terminal_MOESI3.out 2>&1 &
# # wait

# source test.sh MSI 16 > terminal_MSI4.out 2>&1 &
# source test.sh MESI 16 > terminal_MESI4.out 2>&1 &
# source test.sh MOESI 16 > terminal_MOESI4.out 2>&1 &
# wait

# source test.sh MSI 32 > terminal_MSI.out 2>&1 &
# source test.sh MESI 32 > terminal_MESI.out 2>&1 &
# source test.sh MOESI 32 > terminal_MOESI.out 2>&1 &
# wait


# # source test.sh MOESI 16 > terminal_MOESI.out 2>&1
# # source test.sh MSI 32 > terminal_MSI.out 2>&1
# # source test.sh MESI 32 > terminal_MESI.out 2>&1
# source test.sh MOESI 32 > terminal_MOESI.out 2>&1


# source test_splash.sh MSI 2 > terminal_MSI_S.out 2>&1 &
# source test_splash.sh MESI 2 > terminal_MESI_S.out 2>&1 &
# source test_splash.sh MOESI 2 > terminal_MOESI_S.out 2>&1 &
# # # wait

# source test_splash.sh MSI 4 > terminal_MSI2_S.out 2>&1 &
# source test_splash.sh MESI 4 > terminal_MESI2_S.out 2>&1 &
# source test_splash.sh MOESI 4 > terminal_MOESI2_S.out 2>&1 &
# # # wait

# source test_splash.sh MSI 8 > terminal_MSI3_S.out 2>&1 &
# source test_splash.sh MESI 8 > terminal_MESI3_S.out 2>&1 &
# source test_splash.sh MOESI 8 > terminal_MOESI3_S.out 2>&1 &
# # # wait

# source test_splash.sh MSI 16 > terminal_MSI4_S.out 2>&1 &
# source test_splash.sh MESI 16 > terminal_MESI4_S.out 2>&1 &
# source test_splash.sh MOESI 16 > terminal_MOESI4_S.out 2>&1 &
# # # wait


# source test_NoC.sh MSI 2 > terminal_MSI.out 2>&1 &
# source test_NoC.sh MESI 2 > terminal_MESI.out 2>&1 &
# source test_NoC.sh MOESI 2 > terminal_MOESI.out 2>&1 &
# # wait

# source test_NoC.sh MSI 4 > terminal_MSI2.out 2>&1 &
# source test_NoC.sh MESI 4 > terminal_MESI2.out 2>&1 &
# source test_NoC.sh MOESI 4 > terminal_MOESI2.out 2>&1 &
# # wait

# source test_NoC.sh MSI 8 > terminal_MSI3.out 2>&1 &
# source test_NoC.sh MESI 8 > terminal_MESI3.out 2>&1 &
# source test_NoC.sh MOESI 8 > terminal_MOESI3.out 2>&1 &
# # wait

# source test_NoC.sh MSI 16 > terminal_MSI4.out 2>&1 &
# source test_NoC.sh MESI 16 > terminal_MESI4.out 2>&1 &
# source test_NoC.sh MOESI 16 > terminal_MOESI4.out 2>&1 &

source test_splash_NoC.sh MSI 2 > terminal_MSI_S.out 2>&1 &
source test_splash_NoC.sh MESI 2 > terminal_MESI_S.out 2>&1 &
source test_splash_NoC.sh MOESI 2 > terminal_MOESI_S.out 2>&1 &
# wait

source test_splash_NoC.sh MSI 4 > terminal_MSI2_S.out 2>&1 &
source test_splash_NoC.sh MESI 4 > terminal_MESI2_S.out 2>&1 &
source test_splash_NoC.sh MOESI 4 > terminal_MOESI2_S.out 2>&1 &
# wait

source test_splash_NoC.sh MSI 8 > terminal_MSI3_S.out 2>&1 &
source test_splash_NoC.sh MESI 8 > terminal_MESI3_S.out 2>&1 &
source test_splash_NoC.sh MOESI 8 > terminal_MOESI3_S.out 2>&1 &
# wait

source test_splash_NoC.sh MSI 16 > terminal_MSI4_S.out 2>&1 &
source test_splash_NoC.sh MESI 16 > terminal_MESI4_S.out 2>&1 &
source test_splash_NoC.sh MOESI 16 > terminal_MOESI4_S.out 2>&1 &