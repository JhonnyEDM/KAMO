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
set(KAMO_RECOMP_UI_DIR "${KAMO_ROOT}/external/recomp-ui" CACHE PATH "recomp-ui checkout (launcher and online lobby)")

if(NOT EXISTS "${KAMO_GENERATED_DIR}/dispatch_table.cpp")
    message(FATAL_ERROR "No recompiled game in ${KAMO_GENERATED_DIR}. See recomp/README.md.")
endif()
if(NOT EXISTS "${KAMO_BIOS_DIR}/bios_recompiled.cpp")
    message(FATAL_ERROR "No recompiled BIOS in ${KAMO_BIOS_DIR}. See recomp/README.md.")
endif()

# Small KAMO changes to the submodules, kept as patches in recomp/patches
# (gbarecomp-*.patch, recomp-ui-*.patch) and applied to each checkout once. A patch that
# already applies in reverse is skipped.
find_package(Git REQUIRED)
function(kamo_apply_patches prefix checkout)
    file(GLOB patches "${CMAKE_CURRENT_SOURCE_DIR}/patches/${prefix}-*.patch")
    list(SORT patches)
    foreach(patch IN LISTS patches)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
            WORKING_DIRECTORY "${checkout}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT applied EQUAL 0)
            execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                WORKING_DIRECTORY "${checkout}" RESULT_VARIABLE failed)
            if(NOT failed EQUAL 0)
                message(FATAL_ERROR "Cannot apply ${patch} to ${checkout}")
            endif()
            message(STATUS "KAMO: applied ${patch}")
        endif()
    endforeach()
endfunction()
kamo_apply_patches(gbarecomp "${KAMO_GBARECOMP_DIR}")
kamo_apply_patches(recomp-ui "${KAMO_RECOMP_UI_DIR}")

set(GBARECOMP_GENERATED_BIOS_DIR "${KAMO_BIOS_DIR}" CACHE PATH "" FORCE)
set(GBARECOMP_NETPLAY ON CACHE BOOL "" FORCE)
set(GBARECOMP_RUNTIME_UI_ROOT "${KAMO_RECOMP_UI_DIR}" CACHE PATH "" FORCE)
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
# The view probe copies KAMO's compile definitions; give it at least one so the copy is not
# the "defines-NOTFOUND" placeholder.
target_compile_definitions(KAMO PRIVATE KAMO_RECOMP=1)

# KAMOViewProbe: runs 2-4 linked consoles headless from scripted input, optionally over
# loopback netplay (see recomp/README.md). Built only on request.
gbarecomp_target_netplay_view_probe(KAMO src/view_probe_adapter.cpp)

# The launcher window (ROM/BIOS pick, settings, controls) and the online lobby: create or
# join public and private rooms on the lobby server, or play over LAN / direct IP.
# GBARecomp includes recomp_ui.cmake in its own directory scope; include it here as well so the
# asset tables it defines are visible to this target.
set(RECOMP_UI_ROOT "${KAMO_RECOMP_UI_DIR}")
include("${KAMO_RECOMP_UI_DIR}/recomp_ui.cmake")
recomp_target_launcher_ui(KAMO CONSOLE gba)
# The runtime's in-game menu also lives in recomp-ui, so the probe needs it too.
recomp_target_launcher_ui(KAMOViewProbe CONSOLE gba)
# The lobby backend (gbarecomp_target_netplay_launcher() would also add an engine self-test that
# links the game executable, which CMake refuses).
recomp_target_launcher_netplay(KAMO)

# recomp-ui requests C++17 for its targets and its sources rely on it (u8string returning
# std::string); GBARecomp's runtime headers need C++20. Keep the game at C++20 and build
# only recomp-ui's own C++ sources as C++17.
foreach(kamo_target KAMO KAMOViewProbe)
    set_target_properties(${kamo_target} PROPERTIES CXX_STANDARD 20)
    get_target_property(kamo_sources ${kamo_target} SOURCES)
    foreach(kamo_source IN LISTS kamo_sources)
        if(kamo_source MATCHES "recomp-ui/src/.*\\.cpp$")
            set_source_files_properties(${kamo_source} TARGET_DIRECTORY ${kamo_target}
                PROPERTIES COMPILE_OPTIONS "-std=gnu++17")
        endif()
    endforeach()
endforeach()
