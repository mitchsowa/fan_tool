cmake_minimum_required(VERSION 3.10)
project(fan_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Core library shared by the tool and the self-test.
add_library(fan_core
    src/modbus_rtu.cpp
    src/copra_registers.cpp
    src/fan_controller.cpp
    src/command_interpreter.cpp
    src/serial_port_posix.cpp
    src/serial_port_win.cpp
)
target_include_directories(fan_core PUBLIC include)

# The two serial backends guard their own bodies with #ifdef, so it is safe to
# compile both on every platform; only the matching one produces code.

# Main CLI executable.
add_executable(fan_tool src/main.cpp)
target_link_libraries(fan_tool PRIVATE fan_core)

# Hardware-free self-test (CRC, value codecs).
enable_testing()
add_executable(fan_selftest tests/selftest.cpp)
target_link_libraries(fan_selftest PRIVATE fan_core)
add_test(NAME fan_selftest COMMAND fan_selftest)

# End-to-end Modbus loopback over a PTY (POSIX only; needs libutil for openpty).
if(UNIX AND NOT APPLE)
    add_executable(fan_loopback tests/loopback_test.cpp)
    target_link_libraries(fan_loopback PRIVATE fan_core util)
    add_test(NAME fan_loopback COMMAND fan_loopback)
endif()
