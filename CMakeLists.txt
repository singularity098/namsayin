cmake_minimum_required(VERSION 3.24)

project(NAMsayin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# DPF uses GNU extensions (typeof) in its UI code
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  message(FATAL_ERROR "NAMsayin only supports x86-64 (found ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# AVX2/FMA is a hard requirement (see docs/Requirements.md). No fast-math on any platform: the engine relies on
# IEEE behaviour, and both platforms should compute the same results.
if(MSVC)
  add_compile_options(/arch:AVX2 /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
else()
  add_compile_options(-mavx2 -mfma)
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Dependencies

# Our patches to DPF (patches/dpf/, see the README there), applied to the submodule's checkout once. They only change
# Linux code, and a Windows checkout may have CRLF line endings that a patch wouldn't apply to.
if(NOT WIN32)
  find_package(Git REQUIRED)
  file(GLOB NAMSAYIN_DPF_PATCHES ${CMAKE_CURRENT_SOURCE_DIR}/patches/dpf/*.patch)
endif()
list(SORT NAMSAYIN_DPF_PATCHES)
foreach(patch ${NAMSAYIN_DPF_PATCHES})
  execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch}
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/DPF
                  RESULT_VARIABLE NAMSAYIN_PATCH_APPLIED OUTPUT_QUIET ERROR_QUIET)
  if(NOT NAMSAYIN_PATCH_APPLIED EQUAL 0)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply ${patch}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/DPF
                    RESULT_VARIABLE NAMSAYIN_PATCH_FAILED)
    if(NOT NAMSAYIN_PATCH_FAILED EQUAL 0)
      message(FATAL_ERROR "Can't apply ${patch} to deps/DPF (was the submodule updated?)")
    endif()
    message(STATUS "Applied ${patch}")
  endif()
endforeach()

add_subdirectory(deps/DPF)

# NeuralAmpModelerCore is built as a static library from its sources; its own CMake also builds tools we don't need
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/NeuralAmpModelerCore)
file(GLOB_RECURSE NAM_SOURCES CONFIGURE_DEPENDS ${NAM_DIR}/NAM/*.cpp)
add_library(nam STATIC ${NAM_SOURCES})
target_include_directories(nam PUBLIC ${NAM_DIR})
target_include_directories(nam SYSTEM PUBLIC
  ${NAM_DIR}/Dependencies             # <nlohmann/json.hpp>
  ${NAM_DIR}/Dependencies/eigen
  ${NAM_DIR}/Dependencies/nlohmann)   # NAM includes "json.hpp"
target_compile_definitions(nam PUBLIC NAM_ENABLE_A2_FAST NAM_SAMPLE_FLOAT)

# FFTConvolver: zero-latency partitioned convolution for the IR block
set(FFTCONVOLVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/FFTConvolver)
add_library(fftconvolver STATIC
  ${FFTCONVOLVER_DIR}/AudioFFT.cpp
  ${FFTCONVOLVER_DIR}/FFTConvolver.cpp
  ${FFTCONVOLVER_DIR}/TwoStageFFTConvolver.cpp
  ${FFTCONVOLVER_DIR}/Utilities.cpp)
target_include_directories(fftconvolver SYSTEM PUBLIC ${FFTCONVOLVER_DIR})

# miniz: reading zip files in the library. Its CMake generates miniz_export.h; we only need an empty export macro.
set(MINIZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz/miniz_export.h "#pragma once\n#define MINIZ_EXPORT\n")
add_library(miniz STATIC
  ${MINIZ_DIR}/miniz.c
  ${MINIZ_DIR}/miniz_tdef.c
  ${MINIZ_DIR}/miniz_tinfl.c
  ${MINIZ_DIR}/miniz_zip.c)
target_include_directories(miniz SYSTEM PUBLIC ${MINIZ_DIR} ${CMAKE_CURRENT_BINARY_DIR}/miniz)
# 64-bit file offsets, so large archives work
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES _LARGEFILE64_SOURCE)

# xxHash: content hashes (XXH3-128) for favorites and missing-file matching; header-only
add_library(xxhash INTERFACE)
target_include_directories(xxhash SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/xxHash)

# ---------------------------------------------------------------------------------------------------------------------
# Engine (DSP and state, independent of DPF so it can be unit tested)

add_library(namsayin-engine STATIC
  src/engine/BlockRegistry.cpp
  src/engine/blocks/AmbientBlocks.cpp
  src/engine/blocks/BarberPoleBlock.cpp
  src/engine/blocks/BitcrusherBlock.cpp
  src/engine/blocks/ChorusBlock.cpp
  src/engine/blocks/CompressorBlock.cpp
  src/engine/blocks/DelayBlock.cpp
  src/engine/blocks/DelayFxBlocks.cpp
  src/engine/blocks/DoppelgangerBlock.cpp
  src/engine/blocks/DoublerBlock.cpp
  src/engine/blocks/DriveBlocks.cpp
  src/engine/blocks/EchoBlocks.cpp
  src/engine/blocks/EqBlocks.cpp
  src/engine/blocks/FilterBlock.cpp
  src/engine/blocks/FlangerBlock.cpp
  src/engine/blocks/GateBlock.cpp
  src/engine/blocks/IrBlock.cpp
  src/engine/blocks/LooperBlock.cpp
  src/engine/blocks/ModSweeperBlock.cpp
  src/engine/blocks/NamBlock.cpp
  src/engine/blocks/OctaveBlock.cpp
  src/engine/blocks/PhaserBlock.cpp
  src/engine/blocks/PitchBlocks.cpp
  src/engine/blocks/PolyChipBlock.cpp
  src/engine/blocks/ReverbBlock.cpp
  src/engine/blocks/RhythmBlocks.cpp
  src/engine/blocks/RingModBlock.cpp
  src/engine/blocks/RotaryBlock.cpp
  src/engine/blocks/SpringReverbBlock.cpp
  src/engine/blocks/SynthBlock.cpp
  src/engine/blocks/SynthBlocks.cpp
  src/engine/blocks/TalkBoxBlock.cpp
  src/engine/blocks/TremoloBlock.cpp
  src/engine/blocks/VolumeBlocks.cpp
  src/engine/blocks/WahBlocks.cpp
  src/engine/blocks/WarbleBlock.cpp
  src/engine/Engine.cpp
  src/engine/ParallelWorker.cpp
  src/engine/TempoSync.cpp
  src/engine/metronome/Metronome.cpp
  src/engine/tuner/PitchDetector.cpp
  src/engine/tuner/Tuner.cpp
  src/engine/Params.cpp
  src/state/ChainExchange.cpp
  src/state/ChainState.cpp
  src/state/Library.cpp
  src/state/LibraryScanner.cpp
  src/state/Paths.cpp
  src/state/Preset.cpp
  src/state/Settings.cpp)
target_include_directories(namsayin-engine PUBLIC src)
# Whole archive: NAM Core registers its model architectures from static initializers, which the linker would
# otherwise drop ("No config parser registered for architecture")
find_package(Threads REQUIRED)
target_link_libraries(namsayin-engine PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>" fftconvolver miniz xxhash Threads::Threads)

set(NAMSAYIN_WARNINGS $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall -Wextra -Wpedantic>)
target_compile_options(namsayin-engine PRIVATE ${NAMSAYIN_WARNINGS})

# ---------------------------------------------------------------------------------------------------------------------
# Plugin

dpf_add_plugin(namsayin
  TARGETS lv2 vst3
  USE_FILE_BROWSER
  FILES_DSP
    src/plugin/NAMsayinPlugin.cpp
  FILES_UI
    src/ui/BackgroundImage.cpp
    src/ui/BlockGlyphs.cpp
    src/ui/BlockImages.cpp
    src/ui/FolderPicker.cpp
    src/ui/FontData.cpp
    src/ui/ParamHelp.cpp
    src/ui/NAMsayinUI.cpp
    src/ui/OpenUrl.cpp
    src/ui/TextEdit.cpp
    src/ui/Vsync.cpp)

target_include_directories(namsayin PUBLIC src src/plugin)

# MSVC has no glext.h; use our copy instead of the one DPF downloads at configure time
if(MSVC)
  target_include_directories(dgl-system-libs-definitions BEFORE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/khronos)
endif()
target_link_libraries(namsayin PUBLIC namsayin-engine)
if(WIN32)
  target_link_libraries(namsayin PUBLIC shell32 ole32) # ShellExecute (Fretbored link); IFileOpenDialog (folder picker)
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Tests

option(NAMSAYIN_BUILD_TESTS "Build unit tests" ON)
if(NAMSAYIN_BUILD_TESTS)
  enable_testing()
  add_executable(namsayin-tests
    tests/TestMain.cpp
    tests/BlockGlyphTests.cpp
    tests/BlockSanityTests.cpp
    tests/ChainStateTests.cpp
    tests/CompressorBlockTests.cpp
    tests/DelayBlockTests.cpp
    tests/DoppelgangerBlockTests.cpp
    tests/DriveTests.cpp
    tests/DspTests.cpp
    tests/EngineTests.cpp
    tests/IrBlockTests.cpp
    tests/LibraryTests.cpp
    tests/MetronomeTests.cpp
    tests/MorePedalTests.cpp
    tests/NamBlockTests.cpp
    tests/ParamHelpTests.cpp
    tests/PedalTests.cpp
    tests/PresetTests.cpp
    tests/ReverbBlockTests.cpp
    tests/SettingsTests.cpp
    tests/SynthTests.cpp
    tests/TextEditTests.cpp
    tests/TunerTests.cpp
    tests/UndoStackTests.cpp
    src/ui/BlockGlyphs.cpp
    src/ui/ParamHelp.cpp
    src/ui/TextEdit.cpp)
  target_include_directories(namsayin-tests PRIVATE tests deps/doctest src)
  target_link_libraries(namsayin-tests PRIVATE namsayin-engine)
  target_compile_options(namsayin-tests PRIVATE ${NAMSAYIN_WARNINGS})
  target_compile_definitions(namsayin-tests PRIVATE NAMSAYIN_EXAMPLE_MODELS="${NAM_DIR}/example_models")
  add_test(NAME namsayin-tests COMMAND namsayin-tests)
endif()
