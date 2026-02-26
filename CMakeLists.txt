cmake_minimum_required(VERSION 3.20)
project(electroforge VERSION 0.1.0 LANGUAGES C)

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
add_library(ef_compiler_flags INTERFACE)
target_compile_options(ef_compiler_flags INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# ---------------------------------------------------------------------------
# AddressSanitizer — enabled by default in Debug builds
# Can be disabled with -DEF_ASAN=OFF
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(EF_ASAN "Enable AddressSanitizer" ON)
else()
    option(EF_ASAN "Enable AddressSanitizer" OFF)
endif()

add_library(ef_asan_flags INTERFACE)
if(EF_ASAN)
    message(STATUS "AddressSanitizer: ENABLED")
    target_compile_options(ef_asan_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(ef_asan_flags INTERFACE -fsanitize=address)
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
add_library(ef_core STATIC
    src/core/array.c
    src/core/string_builder.c
    src/core/error.c
    src/core/log.c
    src/core/manifest.c
)
target_include_directories(ef_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ef_core
    PRIVATE ef_compiler_flags
    PRIVATE ef_asan_flags
)

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
add_executable(electroforge
    src/main.c
    src/ui/app_window.c
)
target_include_directories(electroforge PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(electroforge
    PRIVATE ef_core
    PRIVATE PkgConfig::GTK4
    PRIVATE ef_compiler_flags
    PRIVATE ef_asan_flags
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# Helper macro — creates one test executable per source file
macro(ef_add_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(${name}
        PRIVATE ef_core
        PRIVATE ef_compiler_flags
        PRIVATE ef_asan_flags
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1"
    )
endmacro()

ef_add_test(test_array       tests/test_array.c)
ef_add_test(test_string_builder tests/test_string_builder.c)
ef_add_test(test_manifest    tests/test_manifest.c)

# ---------------------------------------------------------------------------
# Custom 'tests' target that builds + runs all tests
# ---------------------------------------------------------------------------
add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_array test_string_builder test_manifest
    COMMENT "Building and running all ElectroForge tests"
)
