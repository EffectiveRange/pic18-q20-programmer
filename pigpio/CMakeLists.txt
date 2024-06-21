
find_library(PIGPIO_LIBRARY NAMES pigpio)

if(PIGPIO_LIBRARY)
    message(STATUS "Found pigpio library: ${PIGPIO_LIBRARY}")
    # Include the library in your project
    add_library(pigpioimpl STATIC PiGPIO.cpp)

    target_link_libraries(pigpioimpl PUBLIC igpio PRIVATE fmt::fmt)
    
    target_compile_definitions(pigpioimpl PRIVATE FMT_HEADER_ONLY)

    target_include_directories(pigpioimpl PRIVATE  include)


    target_link_libraries(picprogrammer icsp ${PIGPIO_LIBRARY} rt pigpioimpl)
    find_path(PIGIO_INCLUDE_DIR pigpio.h REQUIRED)
    target_include_directories(picprogrammer PRIVATE   ${PIGIO_INCLUDE_DIR})
else()
    message(STATUS "pigpio library not found, using Mock implementation")
    target_link_libraries(picprogrammer icsp mockgpio)

endif()