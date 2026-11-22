cmake_minimum_required(VERSION 3.10)
project(OpenGLPlayground)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)  # the voxel kernels are ~10x slower in an unoptimised build
endif()

set(CMAKE_CXX_STANDARD 17)
set(OpenGL_GL_PREFERENCE "GLVND")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    find_package(GLEW 2.0 REQUIRED)
endif()
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)

# Include directories for external libraries
include_directories(include ${OPENGL_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS} external/imgui external/ImGuiFileDialog)

# SDL2 and OpenGL libraries
set(IMGUI_SOURCES
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/backends/imgui_impl_sdl2.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
    external/ImGuiFileDialog/ImGuiFileDialog.cpp
)

file(GLOB SOURCES src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_SOURCE_DIR}/src/SceneEdit.cpp ${CMAKE_SOURCE_DIR}/src/ChangeDetect.cpp)

# OpenMP. Apple clang has no OpenMP of its own: use the libomp runtime that ships with Homebrew's llvm
# (or the separate libomp formula). Only omp.h is copied into the build tree so llvm's other clang
# headers never reach Apple clang. On Linux/Windows find_package(OpenMP) works out of the box.
if(APPLE AND NOT OpenMP_CXX_FLAGS)
  foreach(prefix /opt/homebrew/opt/libomp /opt/homebrew/opt/llvm /usr/local/opt/libomp /usr/local/opt/llvm)
    file(GLOB _omp_header ${prefix}/lib/clang/*/include/omp.h ${prefix}/include/omp.h)
    if(EXISTS ${prefix}/lib/libomp.dylib AND _omp_header)
      list(GET _omp_header 0 _omp_header)
      configure_file(${_omp_header} ${CMAKE_BINARY_DIR}/omp_include/omp.h COPYONLY)
      set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${CMAKE_BINARY_DIR}/omp_include")
      set(OpenMP_CXX_LIB_NAMES omp)
      set(OpenMP_omp_LIBRARY ${prefix}/lib/libomp.dylib)
      break()
    endif()
  endforeach()
endif()
find_package(OpenMP COMPONENTS CXX)

# SplatDiff core: scene editing + PLY I/O, no OpenGL (shared by the viewer, CLI tool and tests)
add_library(splatdiff_core STATIC src/SceneEdit.cpp src/ChangeDetect.cpp)
target_include_directories(splatdiff_core PUBLIC include)
target_link_libraries(splatdiff_core PUBLIC glm::glm nlohmann_json::nlohmann_json)
target_compile_definitions(splatdiff_core PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(OpenMP_CXX_FOUND)
  target_link_libraries(splatdiff_core PUBLIC OpenMP::OpenMP_CXX)
else()
  message(WARNING "OpenMP not found: SplatDiff parallel modes will run serially")
endif()

add_executable(splatdiff_detect tools/splatdiff_detect.cpp)
target_link_libraries(splatdiff_detect splatdiff_core)
set_target_properties(splatdiff_detect PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Command-line tool to make scan B from scan A with a ground-truth edit log
add_executable(splatdiff_edit tools/splatdiff_edit.cpp)
target_link_libraries(splatdiff_edit splatdiff_core)
set_target_properties(splatdiff_edit PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Unit tests
add_executable(splatdiff_tests tests/test_scene_edit.cpp)
target_link_libraries(splatdiff_tests splatdiff_core)
set_target_properties(splatdiff_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Disable tests and installation for nlohmann_json
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

# Create imgui static library for target link
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PRIVATE external/imgui)

# Create playground executable
add_executable(splatdiff Main.cpp ${SOURCES})

# Add precompiled header
target_precompile_headers(splatdiff PRIVATE include/OPPCH.h)

# Link libraries
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(splatdiff splatdiff_core imgui ${OPENGL_LIBRARIES} ${SDL2_LIBRARIES} GLEW::GLEW nlohmann_json::nlohmann_json)
else()
    target_link_libraries(splatdiff splatdiff_core imgui ${OPENGL_LIBRARIES} ${SDL2_LIBRARIES} GLEW nlohmann_json::nlohmann_json)
endif()


