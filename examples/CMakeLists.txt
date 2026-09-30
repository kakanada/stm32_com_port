# Сборка хостовых примеров (Linux и Windows):
#   cmake -S . -B build && cmake --build build --config Release
# Результат: example_linux (Linux) или example_windows.exe (Windows).
cmake_minimum_required(VERSION 3.10)
project(scom_examples C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
    set(SCOM_MAIN windows/main_windows.c)
    set(SCOM_TARGET example_windows)
else()
    set(SCOM_MAIN linux/main_linux.c)
    set(SCOM_TARGET example_linux)
endif()

add_executable(${SCOM_TARGET}
    ${SCOM_MAIN}
    monitor/example_monitor.c
    ../host/scom_host.c
    ../host/scom_host_posix.c
    ../host/scom_host_win32.c
    ../common/scom_frame.c
    ../common/scom_crc32.c)

target_include_directories(${SCOM_TARGET} PRIVATE ../common ../host . monitor)

if(MSVC)
    target_compile_options(${SCOM_TARGET} PRIVATE /W4 /utf-8)
    target_compile_definitions(${SCOM_TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${SCOM_TARGET} winmm)
else()
    target_compile_options(${SCOM_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32)
        target_link_libraries(${SCOM_TARGET} winmm)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(${SCOM_TARGET} Threads::Threads)
    endif()
endif()
