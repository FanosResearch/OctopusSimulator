# TileLink simulator component (docs/TileLink_Plan.md §3.1).
# Standalone:   cmake -S TileLink -B <build> && cmake --build <build> && ctest --test-dir <build>
# From Octopus: add_subdirectory(TileLink)  (the tests are then under <build>/TileLink)
cmake_minimum_required(VERSION 3.13)
project(tilelink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TILELINK_BUILD_TESTS "Build the TileLink unit tests" ON)

# The library: every source under src/ (core, agents, check, coh). A new agent or checker is a new
# file, not a CMake edit; re-run cmake to pick it up.
file(GLOB_RECURSE TL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(tilelink STATIC ${TL_SOURCES})
target_include_directories(tilelink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(tilelink PRIVATE -Wall -Wextra)
set_target_properties(tilelink PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(tlsim tools/tlsim.cpp)
target_link_libraries(tlsim PRIVATE tilelink)
target_compile_options(tlsim PRIVATE -Wall -Wextra)

# Tests: one directory per group under test/, each with its own CMakeLists.txt (added here in name
# order). A group's CMakeLists registers its executables with tl_add_test(<group> <name> [sources]),
# which builds test/<group>/<name>.cpp against the library and adds ctest entry <group>.<name>.
if(TILELINK_BUILD_TESTS)
    enable_testing()
    set(TL_TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/test)
    function(tl_add_test group name)
        set(srcs ${ARGN})
        if(NOT srcs)
            set(srcs ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp)   # the calling test directory
        endif()
        add_executable(${group}_${name} ${srcs})
        target_link_libraries(${group}_${name} PRIVATE tilelink)
        target_include_directories(${group}_${name} PRIVATE ${TL_TEST_ROOT}/unit)   # TestMain.h
        target_compile_options(${group}_${name} PRIVATE -Wall -Wextra)
        add_test(NAME ${group}.${name} COMMAND ${group}_${name})
        set_tests_properties(${group}.${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endfunction()
    file(GLOB TL_TEST_DIRS CONFIGURE_DEPENDS LIST_DIRECTORIES true ${TL_TEST_ROOT}/*)
    list(SORT TL_TEST_DIRS)
    foreach(d IN LISTS TL_TEST_DIRS)
        if(IS_DIRECTORY ${d} AND EXISTS ${d}/CMakeLists.txt)
            add_subdirectory(${d})
        endif()
    endforeach()
endif()
