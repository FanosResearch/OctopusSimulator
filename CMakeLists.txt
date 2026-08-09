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
add_compile_definitions(INTERCONNECT=\"Interconnect\")
add_compile_definitions(CACHECONTROLLERS=\"CacheControllers\")
add_compile_definitions(SYSTEM_CONFIGURATIONS=\"SystemConfigurations\")

add_subdirectory(src)
add_subdirectory(header)

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
