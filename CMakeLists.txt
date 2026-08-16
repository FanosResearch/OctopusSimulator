cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(CMSpec_Simulator VERSION 1.0)

add_definitions(-Wno-error=sign-compare)
add_definitions(-Wno-format)

add_compile_definitions(CONFIGURATION_PATH=\"${PROJECT_SOURCE_DIR}/configuration/\")
add_compile_definitions(FSM_PATH=\"${PROJECT_SOURCE_DIR}/Protocols_FSM/\")
add_compile_definitions(INTERCONNECT=\"Interconnect\")
add_compile_definitions(CACHECONTROLLERS=\"CacheControllers\")
add_compile_definitions(SYSTEM_CONFIGURATIONS=\"SystemConfigurations\")

add_subdirectory(src)
add_subdirectory(header)

add_executable(CMSpec_Simulator CMSpec_Simulator.cpp)
add_library(CMSpec SHARED CMSpec_Simulator.cpp)

# target_include_directories(CMSpec_Simulator PUBLIC header)
# target_link_directories(CMSpec_Simulator Private src)

# add_custom_target(copy_resourcs ALL
#             COMMAND ${CMAKE_COMMAND} -E copy_directory
#             ${PROJECT_SOURCE_DIR}/configuration
#             ${PROJECT_BINARY_DIR}/configuration
#             COMMENT "Copying resources into binary directory")

target_link_libraries(CMSpec_Simulator CMSpec_core)
target_link_libraries(CMSpec CMSpec_core)
