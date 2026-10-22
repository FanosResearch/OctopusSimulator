# The TileLink interconnect (docs/TileLink_Plan.md s5.2, s5.3 I-A): the adapter between Octopus's
# CommunicationInterface and the TileLink library (TileLink/, added by the root CMakeLists.txt).
target_sources(Octopus_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/TLPort.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TileLinkInterconnect.cpp
    )
target_include_directories(Octopus_core PUBLIC ${PROJECT_SOURCE_DIR}/header/Interconnect/TileLink)
# the tilelink library is linked in src/CMakeLists.txt (CMP0079: where Octopus_core is defined)
