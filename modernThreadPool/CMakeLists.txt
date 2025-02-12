cmake_minimum_required(VERSION 3.28)
project(modernThreadPool)

set(CMAKE_CXX_STANDARD 17)

add_executable(modernThreadPool main.cpp
        myThreadPool.h)
