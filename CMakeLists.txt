cmake_minimum_required(VERSION 3.16)
project(ADHD-limiter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_executable(adhd_limiter WIN32 src/main.cpp)
    target_compile_definitions(adhd_limiter PRIVATE UNICODE _UNICODE)
else()
    add_executable(adhd_limiter src/non_windows_stub.cpp)
endif()

set_target_properties(adhd_limiter PROPERTIES OUTPUT_NAME "ADHD-limiter")
install(TARGETS adhd_limiter RUNTIME DESTINATION .)
