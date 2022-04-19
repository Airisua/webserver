cmake_minimum_required(VERSION 3.20.6)
project(workspace)

set(CMAKE_CXX_STANDARD 14)

add_executable(workspace main.cpp http/http_conn.cpp timer/timer.h)
