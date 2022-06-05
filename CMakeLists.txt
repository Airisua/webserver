cmake_minimum_required(VERSION 3.20.6)
project(webserver)

set(CMAKE_CXX_STANDARD 14)

aux_source_directory(. CPP_LIST)
aux_source_directory(./buffer CPP_LIST)
aux_source_directory(./epoll CPP_LIST)
aux_source_directory(./http CPP_LIST)
aux_source_directory(./log CPP_LIST)
aux_source_directory(./locker CPP_LIST)
aux_source_directory(./pool CPP_LIST)
aux_source_directory(./timer CPP_LIST)

add_executable(m_server ${CPP_LIST})

#include_directories(/usr/local/include/mysql++)
include_directories(/usr/include/mysql)
link_directories(/usr/lib/x86_64-linux-gnu)
target_link_libraries(webserver libmysqlclient.so)
