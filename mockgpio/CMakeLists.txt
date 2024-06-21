

add_library(mockgpio STATIC src/MockGPIO.cpp src/MockPIC18Q20.cpp)

target_include_directories(mockgpio PUBLIC include)

target_link_libraries(mockgpio PUBLIC igpio icsp PRIVATE fmt::fmt)

target_compile_definitions(mockgpio PRIVATE FMT_HEADER_ONLY)
