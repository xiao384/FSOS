cmake_minimum_required(VERSION 3.20)
project(project_system C ASM_NASM)

set(CMAKE_C_STANDARD 11)

# first 子项目 (OS 内核)
add_subdirectory(first)
