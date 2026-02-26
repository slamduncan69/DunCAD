cmake_minimum_required(VERSION 3.20)
project(duncad VERSION 0.1.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Output directories
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# CMake module path — future Find scripts for GtkSourceView, VTE, OpenGL
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Compiler warnings (applied to all targets via interface library)
# ---------------------------------------------------------------------------
add_library(dc_compiler_flags INTERFACE)
target_compile_options(dc_compiler_flags INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# ---------------------------------------------------------------------------
# AddressSanitizer — enabled by default in Debug builds
# Can be disabled with -DDC_ASAN=OFF
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(DC_ASAN "Enable AddressSanitizer" ON)
else()
    option(DC_ASAN "Enable AddressSanitizer" OFF)
endif()

add_library(dc_asan_flags INTERFACE)
if(DC_ASAN)
    message(STATUS "AddressSanitizer: ENABLED")
    target_compile_options(dc_asan_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(dc_asan_flags INTERFACE -fsanitize=address)
else()
    message(STATUS "AddressSanitizer: DISABLED")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# ---------------------------------------------------------------------------
# Core library (no GTK dependency — usable from tests and CLI)
# ---------------------------------------------------------------------------
add_library(dc_core STATIC
    src/core/array.c
    src/core/string_builder.c
    src/core/error.c
    src/core/log.c
    src/core/manifest.c
)
target_include_directories(dc_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(dc_core
    PRIVATE dc_compiler_flags
    PRIVATE dc_asan_flags
)

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
add_executable(duncad
    src/main.c
    src/ui/app_window.c
)
target_include_directories(duncad PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(duncad
    PRIVATE dc_core
    PRIVATE PkgConfig::GTK4
    PRIVATE dc_compiler_flags
    PRIVATE dc_asan_flags
)

# ---------------------------------------------------------------------------
# Documentation tool (standalone -- no GTK, no dc_core)
# ---------------------------------------------------------------------------
add_executable(duncad-docs tools/duncad_docs.c)
target_link_libraries(duncad-docs PRIVATE dc_compiler_flags)
target_compile_definitions(duncad-docs PRIVATE DC_BUILD_DIR="${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# Helper macro — creates one test executable per source file
macro(dc_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(${name}
        PRIVATE dc_core
        PRIVATE dc_compiler_flags
        PRIVATE dc_asan_flags
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1"
    )
endmacro()

dc_add_test(test_array       tests/test_array.c)
dc_add_test(test_string_builder tests/test_string_builder.c)
dc_add_test(test_manifest    tests/test_manifest.c)

# ---------------------------------------------------------------------------
# Custom 'tests' target that builds + runs all tests
# ---------------------------------------------------------------------------
add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_array test_string_builder test_manifest
    COMMENT "Building and running all DunCAD tests"
)
