cmake_minimum_required(VERSION 3.20)

project(speedtest LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "speedtest needs GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
    message(FATAL_ERROR "GCC 14+ is required for C++23 <print> (found ${CMAKE_CXX_COMPILER_VERSION}). Pass -DCMAKE_CXX_COMPILER=g++-14.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17.0)
    message(FATAL_ERROR "Clang 17+ is required for C++23 <print> (found ${CMAKE_CXX_COMPILER_VERSION}).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(SPEEDTEST_SANITIZE "Build with ASan and UBSan" ON)

if(SPEEDTEST_SANITIZE)
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=address,undefined)
    check_cxx_source_compiles("int main() { return 0; }" HAVE_SANITIZERS)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(NOT HAVE_SANITIZERS)
        message(FATAL_ERROR
            "ASan/UBSan runtimes not found. Install them.\n")
    endif()
endif()

find_package(Threads REQUIRED)

add_executable(speedtest src/main.cpp src/socket.cpp src/receiver.cpp src/sender.cpp src/units.cpp src/stats.cpp src/report.cpp)

target_include_directories(speedtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(speedtest PRIVATE Threads::Threads)

target_compile_options(speedtest PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion)

if(SPEEDTEST_SANITIZE)
    target_compile_definitions(speedtest PRIVATE SPEEDTEST_SANITIZED)
    target_compile_options(speedtest PRIVATE ${SANITIZER_FLAGS})
    target_link_options(speedtest PRIVATE ${SANITIZER_FLAGS})
endif()
