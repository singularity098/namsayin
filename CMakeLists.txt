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

# ---------------------------------------------------------------------------------------------------------------------
# Engine (DSP and state, independent of DPF so it can be unit tested)

add_library(namsayin-engine STATIC
  src/engine/BlockRegistry.cpp
  src/engine/blocks/IrBlock.cpp
  src/engine/blocks/NamBlock.cpp
  src/engine/Engine.cpp
  src/engine/ParallelWorker.cpp
  src/engine/metronome/ClickSamples.cpp
  src/engine/metronome/Metronome.cpp
  src/engine/tuner/PitchDetector.cpp
  src/engine/tuner/Tuner.cpp
  src/engine/Params.cpp
  src/state/ChainExchange.cpp
  src/state/ChainState.cpp)
target_include_directories(namsayin-engine PUBLIC src)
# Whole archive: NAM Core registers its model architectures from static initializers, which the linker would
# otherwise drop ("No config parser registered for architecture")
find_package(Threads REQUIRED)
target_link_libraries(namsayin-engine PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>" fftconvolver Threads::Threads)

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
    src/ui/NAMsayinUI.cpp
    src/ui/OpenUrl.cpp)

target_include_directories(namsayin PUBLIC src src/plugin)

# MSVC has no glext.h; use our copy instead of the one DPF downloads at configure time
if(MSVC)
  target_include_directories(dgl-system-libs-definitions BEFORE INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/khronos)
endif()
target_link_libraries(namsayin PUBLIC namsayin-engine)
if(WIN32)
  target_link_libraries(namsayin PUBLIC shell32) # ShellExecute, for the Fretbored link
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Tests

option(NAMSAYIN_BUILD_TESTS "Build unit tests" ON)
if(NAMSAYIN_BUILD_TESTS)
  enable_testing()
  add_executable(namsayin-tests
    tests/TestMain.cpp
    tests/ChainStateTests.cpp
    tests/DspTests.cpp
    tests/EngineTests.cpp
    tests/IrBlockTests.cpp
    tests/MetronomeTests.cpp
    tests/NamBlockTests.cpp
    tests/TunerTests.cpp)
  target_include_directories(namsayin-tests PRIVATE tests deps/doctest)
  target_link_libraries(namsayin-tests PRIVATE namsayin-engine)
  target_compile_options(namsayin-tests PRIVATE ${NAMSAYIN_WARNINGS})
  target_compile_definitions(namsayin-tests PRIVATE NAMSAYIN_EXAMPLE_MODELS="${NAM_DIR}/example_models")
  add_test(NAME namsayin-tests COMMAND namsayin-tests)
endif()
