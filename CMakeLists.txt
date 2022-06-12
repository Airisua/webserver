cmake_minimum_required(VERSION 3.20.6)
project(workspace)

set(CMAKE_CXX_STANDARD 14)

set(ex
        main_test.cpp
        pool/threadpool.h
        epoll/server.h
        epoll/server.cpp
        http/http_conn.h
        http/http_conn.cpp
        pool/sql_connection_pool.h
        pool/sql_connection_pool.cpp
        timer/timer_list.cpp
        timer/timer_list.h
        locker/locker.h
        buffer/buffer.h
        buffer/buffer.cpp
        log/block_queue.h
        log/log.h
        log/log.cpp
        )

add_executable(workspace ${ex})

#include_directories(/usr/local/include/mysql++)
include_directories(/usr/include/mysql)
link_directories(/usr/lib/x86_64-linux-gnu)
target_link_libraries(workspace libmysqlclient.so)
