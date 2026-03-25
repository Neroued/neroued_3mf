include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(neroued_3mf_tests
    test_types.cpp
    test_builder.cpp
    test_writer.cpp)

target_link_libraries(neroued_3mf_tests PRIVATE neroued_3mf GTest::gtest_main)
target_include_directories(neroued_3mf_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(neroued_3mf_tests)
