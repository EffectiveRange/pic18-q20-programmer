MESSAGE(STATUS "Target arch is ${CMAKE_SYSTEM_PROCESSOR}")

if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
    pkg_check_modules(LIBGPIO REQUIRED libgpiodcxx)
    message(STATUS "Found libgpiod library: ${LIBGPIO_LIBRARIES}")
    # Include the library in your project
    add_library(libgpioimpl STATIC libGPIO.cpp)

    target_link_libraries(libgpioimpl PUBLIC igpio rt ${LIBGPIO_LINK_LIBRARIES} PRIVATE fmt::fmt)
    
    target_compile_definitions(libgpioimpl PRIVATE FMT_HEADER_ONLY)

    target_include_directories(libgpioimpl PUBLIC  ${LIBGPIO_INCLUDE_DIRS} PRIVATE include)

    add_executable(libgpio_test  main.cpp )

    target_link_libraries(libgpio_test libgpioimpl argparse fmt::fmt)


    target_link_libraries(picprogrammer icsp libgpioimpl)
endif()