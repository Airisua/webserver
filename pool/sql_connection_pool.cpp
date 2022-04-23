#include "sql_connection_pool.h"

connection_pool* connection_pool::getInstance(){
    static connection_pool* conn_pool;
    return conn_pool;
}

connection_pool::connection_pool() {
    this->cur_conn = 0;
    this->free_conn = 0;
}

// 构造初始化
void connection_pool::init(std::string url,std::string user,std::string password,std::string data_base_name,int port,unsigned int max_conn)
{
    // 初始化数据库信息
    this->url = url;
    this->user = user;
    this->password = password;
    this->data_base_name = data_base_name;
    this->port = port;

    // 创建 max_conn条数据库连接
    lock.lock();
    for(int i = 0; i < max_conn; ++i) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);

        if(conn == nullptr) {
            std::cout << "Error" << mysql_error(conn);
            exit(1);
        }

        conn = mysql_real_connect()
    }
    lock.unlock();
}

// 销毁数据库连接池
void connection_pool::destroy_pool(){

}

// RAII机制来销毁连接池
connection_pool::~connection_pool() {
    destroy_pool();
}