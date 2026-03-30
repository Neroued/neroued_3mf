cmake_minimum_required(VERSION 3.16)

# -- Version from git tag ----------------------------------------------------
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(GIT_TAG)
        string(REGEX REPLACE "^v" "" N3MF_VERSION "${GIT_TAG}")
        string(REGEX REPLACE "[^0-9.].*" "" N3MF_VERSION "${N3MF_VERSION}")
    endif()
endif()
if(NOT N3MF_VERSION)
    set(N3MF_VERSION "0.1.0")
endif()

project(neroued_3mf VERSION ${N3MF_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# -- Options -----------------------------------------------------------------
option(N3MF_BUILD_TESTS "Build unit tests" ON)
option(N3MF_USE_OPENMP  "Enable OpenMP parallelization" ON)

# -- zlib (optional, enables deflate compression) ----------------------------
find_package(ZLIB QUIET)

# -- OpenMP (optional, enables parallel processing) -------------------------
if(N3MF_USE_OPENMP)
    find_package(OpenMP QUIET)
endif()

# -- Generate version.h -----------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/neroued/3mf/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/neroued/3mf/version.h"
    @ONLY)

# -- Library -----------------------------------------------------------------
add_library(neroued_3mf STATIC
    src/types.cpp
    src/builder.cpp
    src/zip.cpp
    src/opc.cpp
    src/writer.cpp
    src/watermark_detect.cpp)

add_library(neroued::3mf ALIAS neroued_3mf)

target_include_directories(neroued_3mf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(ZLIB_FOUND)
    target_link_libraries(neroued_3mf PRIVATE ZLIB::ZLIB)
    target_compile_definitions(neroued_3mf PRIVATE NEROUED_3MF_HAS_ZLIB=1)
    message(STATUS "neroued_3mf: zlib found, deflate compression enabled")
else()
    message(STATUS "neroued_3mf: zlib not found, using store-only compression")
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(neroued_3mf PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(neroued_3mf PRIVATE NEROUED_3MF_HAS_OPENMP=1)
    message(STATUS "neroued_3mf: OpenMP found, parallel processing enabled")
else()
    message(STATUS "neroued_3mf: OpenMP not found, using serial processing")
endif()

target_compile_options(neroued_3mf PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:RelWithDebInfo>>:-O2 -g>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# -- Tools -------------------------------------------------------------------
option(N3MF_BUILD_TOOLS "Build CLI tools" ON)
if(N3MF_BUILD_TOOLS)
    add_executable(n3mf_detect tools/detect_watermark.cpp)
    target_link_libraries(n3mf_detect PRIVATE neroued_3mf)

    add_executable(n3mf_generate_test tools/generate_test_3mf.cpp)
    target_link_libraries(n3mf_generate_test PRIVATE neroued_3mf)

    set_target_properties(n3mf_detect n3mf_generate_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# -- Tests -------------------------------------------------------------------
if(N3MF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -- Install -----------------------------------------------------------------
install(TARGETS neroued_3mf
    EXPORT neroued_3mf-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/neroued
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")

# Install generated version.h alongside the source headers
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/neroued/3mf/version.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/neroued/3mf")

install(EXPORT neroued_3mf-targets
    NAMESPACE neroued::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/neroued_3mf")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/neroued_3mf-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/neroued_3mf-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/neroued_3mf")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/neroued_3mf-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/neroued_3mf-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/neroued_3mf-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/neroued_3mf")
