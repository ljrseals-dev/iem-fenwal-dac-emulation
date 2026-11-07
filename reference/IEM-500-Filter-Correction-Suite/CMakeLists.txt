# =============================================================================
#  CMakeLists.txt
#
#  Project : IEM-500 Filter Correction Suite
#  Purpose : Build configuration for the filter correction reference library
#            and its regression test executable.
#
#  Targets :
#      iem500_filters  - static library
#      iem500_tests    - regression test executable
#
#  Requirements :
#      CMake 3.16 or later
#      C++17 or later
#
#  Usage :
#      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --config Release
#      ctest --test-dir build --output-on-failure
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(iem500_filters
    VERSION 0.1.0
    DESCRIPTION "IEM-500 filter correction reference implementation"
    LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Language standard
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -----------------------------------------------------------------------------
# Compiler warnings
# -----------------------------------------------------------------------------

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# Core library
# -----------------------------------------------------------------------------

add_library(iem500_filters STATIC
    src/HpfSection.cpp
    src/LpfSection.cpp
    src/JilesAtherton.cpp
    src/Oversampler.cpp
    src/FilterChain.cpp
)

target_include_directories(iem500_filters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(iem500_filters PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# Test executable
# -----------------------------------------------------------------------------

add_executable(iem500_tests
    test/test_suite.cpp
)

target_link_libraries(iem500_tests PRIVATE iem500_filters)

# -----------------------------------------------------------------------------
# CTest integration
# -----------------------------------------------------------------------------

enable_testing()

add_test(NAME iem500_tests COMMAND iem500_tests)
set_tests_properties(iem500_tests PROPERTIES TIMEOUT 120)
