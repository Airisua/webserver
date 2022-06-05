cmake_minimum_required(VERSION 3.20.6)
project(workspace)

set(CMAKE_CXX_STANDARD 14)

# set(ex1 main.cpp http/http_conn.cpp timer/timer.h pool/sql_connection_pool.h pool/sql_connection_pool.cpp)
# set(ex2 ${ex1} timer/timer_list.cpp timer/timer_list.h pool/thread_test.h epoll/server.h epoll/server.cpp)
# set(ex3 ${ex2} main_test.cpp log/block_queue.h log/test.h log/log.h log/log.cpp buffer/buffer.h buffer/buffer.cpp)
# set(ex4 ${ex3})

add_executable(workspace ${ex4})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

#include_directories(/usr/local/include/mysql++)
include_directories(/usr/include/mysql)
link_directories(/usr/lib/x86_64-linux-gnu)
target_link_libraries(workspace libmysqlclient.so)
