cmake_minimum_required(VERSION 3.20.6)
project(workspace)

set(CMAKE_CXX_STANDARD 14)

set(executable main.cpp http/http_conn.cpp timer/timer.h pool/sql_connection_pool.h pool/sql_connection_pool.cpp timer/timer_test.h log/block_queue.h)
set(test ${executable} )
#include_directories(/usr/lib/mysql)
add_executable(workspace ${executable})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

#include_directories(/usr/local/include/mysql++)
include_directories(/usr/include/mysql)  # 头文件
link_directories(/usr/lib/x86_64-linux-gnu)  # 动态库
target_link_libraries(workspace libmysqlclient.so)