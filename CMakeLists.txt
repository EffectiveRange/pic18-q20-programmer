cmake_minimum_required(VERSION 3.22)

include(ERBuild NO_POLICY_SCOPE)

set(picprogrammer_ver 0.5.2)

project(picprogrammer VERSION ${picprogrammer_ver} DESCRIPTION "This project can be used to program the onboard PIC microcontroller")


set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

ER_DEPS()

ER_ADD_EXECUTABLE(picprogrammer SOURCES src/main.cpp src/prog_utils.cpp )

find_package(fmt REQUIRED)

find_package(Catch2  REQUIRED)

find_package(PkgConfig REQUIRED)

# fetch latest argparse
include(FetchContent)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)


add_subdirectory(igpio)

add_subdirectory(icsp)

add_subdirectory(mockgpio)

add_subdirectory(pigpio)

add_subdirectory(libgpiod)




target_link_libraries(picprogrammer icsp igpio fmt::fmt argparse mockgpio )


target_include_directories(picprogrammer PRIVATE  include)

target_compile_definitions(picprogrammer PRIVATE -DPICPROG_VER="${picprogrammer_ver}" FMT_HEADER_ONLY)

add_executable(icsp_test test/test_ICSP.cpp  test/test_utils.cpp test/test_intelhex.cpp test/test_PICProgrammer.cpp test/test_mockimpl.cpp)

target_link_libraries(icsp_test PRIVATE Catch2::Catch2WithMain mockgpio icsp fmt::fmt)

target_compile_definitions(icsp_test PRIVATE FMT_HEADER_ONLY)

ER_ENABLE_TEST()

add_test(test_icsp icsp_test )




ER_PACK()
