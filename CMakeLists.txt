cmake_minimum_required(VERSION 3.22)
project(file_system C)

set(CMAKE_C_STANDARD 11)

add_executable(file_system main.c
        file_sys.c
        file_sys.h)
