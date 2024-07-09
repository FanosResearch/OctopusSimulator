cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=sign-compare")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(CMSpec_Simulator VERSION 1.0)

add_subdirectory(src)
add_subdirectory(header)

add_executable(CMSpec_Simulator CMSpec_Simulator.cpp)
add_library(CMSpec SHARED CMSpec_Simulator.cpp)

# target_include_directories(CMSpec_Simulator PUBLIC header)
# target_link_directories(CMSpec_Simulator Private src)

target_link_libraries(CMSpec_Simulator CMSpec_core)
target_link_libraries(CMSpec CMSpec_core)
