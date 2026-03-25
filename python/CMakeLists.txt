# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 neroued

cmake_minimum_required(VERSION 3.16...3.30)
project(neroued_3mf_python LANGUAGES CXX)

set(N3MF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/neroued_3mf)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_neroued_3mf STABLE_ABI NB_STATIC src/bind.cpp)
target_link_libraries(_neroued_3mf PRIVATE neroued_3mf)
target_compile_features(_neroued_3mf PRIVATE cxx_std_20)

install(TARGETS _neroued_3mf LIBRARY DESTINATION neroued_3mf)

nanobind_add_stub(
    _neroued_3mf_stub
    MODULE _neroued_3mf
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stubs/_neroued_3mf.pyi"
    PYTHON_PATH "$<TARGET_FILE_DIR:_neroued_3mf>"
    DEPENDS _neroued_3mf
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stubs/_neroued_3mf.pyi"
        DESTINATION neroued_3mf)
