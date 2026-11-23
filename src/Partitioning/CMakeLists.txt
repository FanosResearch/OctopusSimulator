# Partitioning schemes and ownerships register themselves by name (static registrars). They are
# an OBJECT library linked straight into the final binaries: inside the static Octopus_core
# archive the linker would drop a scheme's object file, since nothing references it by symbol.
# Every .cpp here is built, so a new scheme is one new file (re-run cmake to pick it up).
file(GLOB OCTOPUS_PARTITIONING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(Octopus_partitioning OBJECT ${OCTOPUS_PARTITIONING_SOURCES})
target_include_directories(Octopus_partitioning PRIVATE
            ${PROJECT_SOURCE_DIR}/header
            ${PROJECT_SOURCE_DIR}/header/Partitioning)
