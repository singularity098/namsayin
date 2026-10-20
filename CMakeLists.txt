cmake_minimum_required(VERSION 3.22)

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

# AVX2/FMA is a hard requirement (see docs/Requirements.md)
if(MSVC)
  add_compile_options(/arch:AVX2 /fp:fast /utf-8)
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
target_include_directories(nam SYSTEM PUBLIC ${NAM_DIR}/Dependencies/eigen ${NAM_DIR}/Dependencies/nlohmann)
target_compile_definitions(nam PUBLIC NAM_ENABLE_A2_FAST)

# ---------------------------------------------------------------------------------------------------------------------
# Plugin

dpf_add_plugin(namsayin
  TARGETS lv2 vst3
  FILES_DSP
    src/plugin/NAMsayinPlugin.cpp
  FILES_UI
    src/ui/NAMsayinUI.cpp)

target_include_directories(namsayin PUBLIC src src/plugin)
target_link_libraries(namsayin PUBLIC nam)
