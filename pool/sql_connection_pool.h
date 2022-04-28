#ifndef WORKSPACE_SQL_CONNECTION_POOL_H
#define WORKSPACE_SQL_CONNECTION_POOL_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <list>
#include <mysql/mysql.h>
#include "../locker/locker.h"

// 创建连接池
class connection_pool {
public:
    MYSQL* get_connection(); //获取数据库连接
    bool release_connection(MYSQL* conn); //释放连接
    void destroy_pool(); // 销毁所有连接
    int get_free_conn(); // 获取空闲连接

    // 构造初始化
    void init(std::string url,std::string user,std::string password,std::string data_base_name,int port,unsigned int max_conn);

    // 单例模式
    static connection_pool* getInstance();

private:
    connection_pool();
    ~connection_pool();

private:
    int cur_conn;  //当前已使用的连接数
    int free_conn; //当前空闲的连接数
    int max_conn;  // 最大连接数

private:
    locker lock;
    std::list<MYSQL*>conn_list; // 连接池
    sem reserve;

private:
    std::string url;            // 主机地址
    std::string port;           // 数据库端口
    std::string user;           // 登录数据用户名
    std::string password;       // 登录数据库密码
    std::string data_base_name; // 使用的数据库名

};

class connection_RAII{
public:
    // 双指针对MYSQL *con修改
    connection_RAII(MYSQL **conn,connection_pool *conn_pool);
    ~connection_RAII();

private:
    MYSQL* conn_RAII;
    connection_pool *pool_RAII;
};


#endif //WORKSPACE_SQL_CONNECTION_POOL_H
