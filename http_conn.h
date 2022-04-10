#ifndef WORKSPACE_HTTP_CONN_H
#define WORKSPACE_HTTP_CONN_H

#include <cstdio>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

class http_conn {
public:
    http_conn() {}
    ~http_conn(){}

public:
    void process(); // 处理客户端请求
    void init(int sock_fd,const sockaddr_in& address); // 初始化新接受的连接
    void close_conn(); // 关闭连接
    bool read(); // 非阻塞读
    bool write(); // 非阻塞写

public:
    static int m_epoll_fd;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count;  // 统计用户的数量
private:
    int m_sock_fd; // 该HTTP连接的socket和对方的socket地址
    sockaddr_in m_address;

};

#endif //WORKSPACE_HTTP_CONN_H
