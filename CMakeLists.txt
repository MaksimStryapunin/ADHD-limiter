cmake_minimum_required(VERSION 3.20)
project(ADHDLimiter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_executable(adhd_limiter WIN32 src/main.cpp)
    target_compile_definitions(adhd_limiter PRIVATE UNICODE _UNICODE)
    set_target_properties(adhd_limiter PROPERTIES OUTPUT_NAME "ADHD-limiter")

    install(TARGETS adhd_limiter RUNTIME DESTINATION .)

    set(CPACK_PACKAGE_NAME "ADHD-limiter")
    set(CPACK_PACKAGE_VENDOR "ADHD-limiter")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "ADHD-limiter-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
    set(CPACK_GENERATOR "ZIP;TGZ")
    include(CPack)
else()
    add_executable(adhd_limiter src/non_windows_stub.cpp)
endif()
