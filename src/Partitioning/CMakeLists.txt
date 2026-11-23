# Partitioning schemes and ownerships register themselves by name (static registrars). They are
# an OBJECT library linked straight into the final binaries: inside the static Octopus_core
# archive the linker would drop a scheme's object file, since nothing references it by symbol.
# A new scheme is one more file in this list.
add_library(Octopus_partitioning OBJECT
            ${CMAKE_CURRENT_SOURCE_DIR}/Partitioning.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/WayPartition.cpp
    )
target_include_directories(Octopus_partitioning PRIVATE
            ${PROJECT_SOURCE_DIR}/header
            ${PROJECT_SOURCE_DIR}/header/Partitioning)
