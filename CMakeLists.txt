cmake_minimum_required(VERSION 3.16)
project(ADHD-limiter VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE)

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_executable(adhd_limiter WIN32 src/main.cpp resource.rc)
    target_include_directories(adhd_limiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(adhd_limiter PRIVATE winmm)
else()
    add_executable(adhd_limiter src/non_windows_stub.cpp)
endif()

set_target_properties(adhd_limiter PROPERTIES OUTPUT_NAME "ADHD-limiter")
install(TARGETS adhd_limiter RUNTIME DESTINATION .)
