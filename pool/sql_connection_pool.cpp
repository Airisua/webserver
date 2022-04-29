#include "sql_connection_pool.h"

connection_pool* connection_pool::getInstance(){
    static connection_pool conn_pool;
    return &conn_pool;
}

connection_pool::connection_pool() : cur_conn(0),free_conn(0),max_conn(0) {}

// 初始化
void connection_pool::init(std::string url, std::string user, std::string password, std::string data_base_name,
                           unsigned int port, unsigned int max_conn)
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

//        if(!mysql_real_connect(conn,"localhost","root","123456","test_db", 3306,nullptr,0)) {
//            printf("error: %s\n", mysql_error(conn));
//        }

        conn = mysql_real_connect(conn,url.c_str(),user.c_str(),password.c_str(),data_base_name.c_str(),port, nullptr,0);

        if(conn == nullptr) {
            std::cout << "Error" << mysql_error(conn);
            exit(-1);
        }
        std::cout << "11" << std::endl;
        // 更新连接池和空闲连接数量
        conn_list.push_back(conn);
        ++free_conn;

    }
    // 将信号量初始化为最大连接次数
    reserve = sem(free_conn);
    this->max_conn = free_conn;

    lock.unlock();
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::get_connection() {
    MYSQL* conn = nullptr;

    if(conn_list.empty()) return nullptr;

    // 取出连接，信号量原子减1，为0则等待
    reserve.wait();

    lock.lock();
    conn = conn_list.front();
    conn_list.pop_front();
    --free_conn;
    ++cur_conn;

    lock.unlock();
    return conn;
}

// 释放当前使用的连接
bool connection_pool::release_connection(MYSQL *conn) {
    if(conn == nullptr) return false;

    lock.lock();

    conn_list.push_back(conn);
    ++free_conn;
    --cur_conn;

    lock.unlock();

    // 释放连接原子加1
    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::destroy_pool(){
    lock.lock();
    if(!conn_list.empty()) {
        // 迭代器遍历，关闭数据库连接
        for(auto conn : conn_list) {
            mysql_close(conn);
        }
        cur_conn = 0;
        free_conn = 0;

        // 清空list;
        conn_list.clear();
        lock.unlock();
    }

    lock.unlock();
}

// 当前空闲连接数
int connection_pool::get_free_conn() const {
    return this->free_conn;
}

// RAII机制来销毁连接池
connection_pool::~connection_pool() {
    destroy_pool();
}

connection_RAII::connection_RAII(MYSQL **SQL, connection_pool *conn_pool) {
    *SQL = conn_pool->get_connection();

    conn_RAII = *SQL;
    pool_RAII = conn_pool;
}

connection_RAII::~connection_RAII() {
    pool_RAII->release_connection(conn_RAII);
}