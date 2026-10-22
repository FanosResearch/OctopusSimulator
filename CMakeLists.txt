cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default to an optimized build. Without this the toolchain defaults to -O0,
# which makes the simulator ~3-4x slower for no benefit. Configure with
# -DCMAKE_BUILD_TYPE=Debug to override for debugging.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

project(Octopus_Simulator VERSION 1.0)

add_definitions(-Wno-error=sign-compare)
add_definitions(-Wno-format)

add_compile_definitions(CONFIGURATION_PATH=\"${PROJECT_SOURCE_DIR}/configuration/\")
add_compile_definitions(FSM_PATH=\"${PROJECT_SOURCE_DIR}/Protocols_FSM/\")
add_compile_definitions(MCSIM_PATH=\"${PROJECT_SOURCE_DIR}/src/MCsim/\")
add_compile_definitions(INTERCONNECT=\"Interconnect\")
add_compile_definitions(CACHECONTROLLERS=\"CacheControllers\")
add_compile_definitions(SYSTEM_CONFIGURATIONS=\"SystemConfigurations\")

# The TileLink library (docs/TileLink_Plan.md s3.1): its own CMake project, linked into Octopus_core
# (src/Interconnect/TileLink). Its unit tests stay off inside Octopus unless asked for
# (-DTILELINK_BUILD_TESTS=ON; they are then under <build>/TileLink).
option(TILELINK_BUILD_TESTS "Build the TileLink library's unit tests" OFF)
add_subdirectory(TileLink)

add_subdirectory(src)
add_subdirectory(header)
add_subdirectory(test/network/unit)   # Octopus_tests (docs/NoC_Test_Plan.md)
add_subdirectory(test/tilelink/unit)  # Octopus_tl_tests (TLPort / TileLinkInterconnect, docs/TileLink_Plan.md s5.2)

add_executable(Octopus_Simulator Octopus_Simulator.cpp)
add_library(Octopus SHARED Octopus_Simulator.cpp)

# target_include_directories(Octopus_Simulator PUBLIC header)
# target_link_directories(Octopus_Simulator Private src)

# add_custom_target(copy_resourcs ALL
#             COMMAND ${CMAKE_COMMAND} -E copy_directory
#             ${PROJECT_SOURCE_DIR}/configuration
#             ${PROJECT_BINARY_DIR}/configuration
#             COMMENT "Copying resources into binary directory")

target_link_libraries(Octopus_Simulator Octopus_core)
target_link_libraries(Octopus Octopus_core)
