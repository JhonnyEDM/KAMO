cmake_minimum_required(VERSION 3.20)
# KAMO on GBARecomp: the statically recompiled game linked to the GBARecomp runtime.
#
# Generated inputs come from the player's own ROM and BIOS and are never committed:
#   KAMO_GENERATED_DIR   output of `gbarecomp build --rom <rom>` (its generated/ folder)
#   KAMO_BIOS_DIR        output of `gba_recompile --bios <bios> --config bios/gba_bios.toml`
project(KAMORecomp C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KAMO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(KAMO_GBARECOMP_DIR "${KAMO_ROOT}/external/gbarecomp" CACHE PATH "GBARecomp checkout")
set(KAMO_GENERATED_DIR "${KAMO_ROOT}/generated/b8ke/generated" CACHE PATH "Recompiled cartridge sources")
set(KAMO_BIOS_DIR "${KAMO_ROOT}/generated/bios" CACHE PATH "Recompiled BIOS sources")

if(NOT EXISTS "${KAMO_GENERATED_DIR}/dispatch_table.cpp")
    message(FATAL_ERROR "No recompiled game in ${KAMO_GENERATED_DIR}. See recomp/README.md.")
endif()
if(NOT EXISTS "${KAMO_BIOS_DIR}/bios_recompiled.cpp")
    message(FATAL_ERROR "No recompiled BIOS in ${KAMO_BIOS_DIR}. See recomp/README.md.")
endif()

set(GBARECOMP_GENERATED_BIOS_DIR "${KAMO_BIOS_DIR}" CACHE PATH "" FORCE)
set(GBARECOMP_NETPLAY ON CACHE BOOL "" FORCE)
add_subdirectory("${KAMO_GBARECOMP_DIR}" gbarecomp EXCLUDE_FROM_ALL)

# The recompiled cartridge, compiled once and shared by the game and its test tools.
file(GLOB KAMO_SHARDS CONFIGURE_DEPENDS "${KAMO_GENERATED_DIR}/recompiled_*.cpp")
add_library(kamo_generated STATIC ${KAMO_SHARDS} "${KAMO_GENERATED_DIR}/dispatch_table.cpp")
target_include_directories(kamo_generated PRIVATE
    "${KAMO_GENERATED_DIR}"
    $<TARGET_PROPERTY:gbarecomp_runtime,INTERFACE_INCLUDE_DIRECTORIES>
    "${KAMO_GBARECOMP_DIR}/src/armv4t")
target_compile_definitions(kamo_generated PRIVATE
    $<TARGET_PROPERTY:gbarecomp_runtime,INTERFACE_COMPILE_DEFINITIONS>)

set(KAMO_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${KAMO_GBARECOMP_DIR}/src/armv4t"
    "${KAMO_GBARECOMP_DIR}/src/runtime"
    "${KAMO_GBARECOMP_DIR}/src/gba")

add_executable(KAMO src/main.cpp src/kamo_game.cpp)
target_include_directories(KAMO PRIVATE ${KAMO_INCLUDES})
target_link_gbarecomp_runtime_stack(KAMO kamo_generated gbarecomp_netplay)
gbarecomp_target_netplay_identity(KAMO "${CMAKE_CURRENT_SOURCE_DIR}" "${KAMO_GENERATED_DIR}")
gbarecomp_stage_mingw_runtime(KAMO)

# KAMOViewProbe: runs 2-4 linked consoles headless from scripted input, optionally over
# loopback netplay (see recomp/README.md). Built only on request.
gbarecomp_target_netplay_view_probe(KAMO src/view_probe_adapter.cpp)
