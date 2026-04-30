cmake_minimum_required(VERSION 3.16)
project(vdi_wrapper)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_executable(vdi_wrapper
    src/main.cpp
)
