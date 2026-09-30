# Сборка примера на Linux и Windows:
#   cmake -S . -B build && cmake --build build --config Release
cmake_minimum_required(VERSION 3.10)
project(scom_host_example C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(host_example
    host_example.c
    ../host/scom_host.c
    ../host/scom_host_posix.c
    ../host/scom_host_win32.c
    ../common/scom_frame.c
    ../common/scom_crc32.c)

target_include_directories(host_example PRIVATE ../common ../host .)

if(MSVC)
    target_compile_options(host_example PRIVATE /W4 /utf-8)
    target_link_libraries(host_example winmm)
    target_compile_definitions(host_example PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(host_example PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32)
        target_link_libraries(host_example winmm)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(host_example Threads::Threads)
    endif()
endif()
