cmake_minimum_required(VERSION 3.10)
project(sshx C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_BUILD_TYPE "Debug")

add_executable(sshx
        sshx.c
        )