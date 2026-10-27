cmake_minimum_required(VERSION 3.21)
project(FightingForOnePiece_Recomp LANGUAGES C CXX)

set(FFOP_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH
    "Output of ps2_recomp for SLPS_255.45 (scripts/recomp.sh), VU1 microcode in vu1/ (scripts/compile_vu1.sh)")

set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "Enable very verbose/agressive PS2 runtime logs")

add_subdirectory(PS2Recomp)

file(GLOB FFOP_GENERATED_SOURCES CONFIGURE_DEPENDS "${FFOP_GENERATED_DIR}/*.cpp")
if(NOT FFOP_GENERATED_SOURCES)
    message(FATAL_ERROR "No generated sources in ${FFOP_GENERATED_DIR}. Run scripts/recomp.sh first.")
endif()

# The runtime's placeholder function table clashes with the generated register_functions.cpp.
get_target_property(_ffop_runner_sources ps2EntryRunner SOURCES)
list(FILTER _ffop_runner_sources EXCLUDE REGEX "src/runner/register_functions\\.cpp$")
set_property(TARGET ps2EntryRunner PROPERTY SOURCES ${_ffop_runner_sources})

file(GLOB FFOP_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/*.cpp")

file(GLOB FFOP_VU1_SOURCES CONFIGURE_DEPENDS "${FFOP_GENERATED_DIR}/vu1/*.cpp")
if(FFOP_VU1_SOURCES)
    set_source_files_properties(${FFOP_VU1_SOURCES} PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON
        COMPILE_OPTIONS "-O3;-g0")
else()
    message(STATUS "FightingForOnePiece_Recomp: no compiled VU1 microcode in ${FFOP_GENERATED_DIR}/vu1, "
        "VU1 runs in the interpreter (scripts/compile_vu1.sh)")
endif()

target_sources(ps2EntryRunner PRIVATE ${FFOP_GENERATED_SOURCES} ${FFOP_RUNTIME_SOURCES} ${FFOP_VU1_SOURCES})
target_include_directories(ps2EntryRunner PRIVATE "${FFOP_GENERATED_DIR}")

list(LENGTH FFOP_GENERATED_SOURCES _ffop_count)
list(LENGTH FFOP_VU1_SOURCES _ffop_vu1_count)
message(STATUS "FightingForOnePiece_Recomp: ${_ffop_count} generated sources, ${_ffop_vu1_count} compiled VU1 file(s)")
