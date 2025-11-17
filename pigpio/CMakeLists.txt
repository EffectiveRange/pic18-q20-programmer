
if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "armhf")
    find_library(PIGPIO_LIBRARY NAMES pigpio REQUIRED)
    message(STATUS "Found pigpio library: ${PIGPIO_LIBRARY}")
    # Include the library in your project
    add_library(pigpioimpl OBJECT PiGPIO.cpp)

    target_link_libraries(pigpioimpl PUBLIC igpio PRIVATE fmt::fmt  ${PIGPIO_LIBRARY} rt)
    
    target_compile_definitions(pigpioimpl PRIVATE FMT_HEADER_ONLY)
    
    find_path(PIGIO_INCLUDE_DIR pigpio.h REQUIRED)
    target_include_directories(pigpioimpl PRIVATE  include   ${PIGIO_INCLUDE_DIR})


    target_link_libraries(picprogrammer pigpioimpl)
endif()