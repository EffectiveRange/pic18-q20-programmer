add_library(icsp STATIC src/ICSP_header.cpp src/PICProgrammer.cpp src/utils.cpp src/IntelHex.cpp)

target_include_directories(icsp PUBLIC include)

target_link_libraries(icsp PUBLIC igpio PRIVATE fmt::fmt)

target_compile_definitions(icsp PRIVATE FMT_HEADER_ONLY)
