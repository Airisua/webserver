#include "http_conn.h"

 int http_conn::m_epoll_fd = -1;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
 int http_conn::m_user_count = 0;  // 所有的客户数

// 设置文件描述符非阻塞
void set_nonblocking(int fd) {
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
}

// 向epoll中添加需要监听的文件描述符
void add_fd(int epoll_fd,int fd,bool one_shot){
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event);
    // 设置文件描述符非阻塞
    set_nonblocking(fd);
}
// 从epoll中移除监听的文件描述符
void remove_fd(int epoll_fd,int fd){
    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,fd,nullptr);
    close(fd);
}
// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void mod_fd(int epoll_fd,int fd,int ev) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&event);
}
// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sock_fd, const sockaddr_in &address){
    m_sock_fd = sock_fd;
    m_address = address;
    // 端口复用
    int reuse = 1;
    setsockopt(m_sock_fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof (reuse));
    // 添加到epoll对象中
    add_fd(m_epoll_fd,sock_fd,true);
    m_user_count++;  // 总用户数+1
}

// 关闭连接
void http_conn::close_conn() {
    if(m_sock_fd != -1){
        remove_fd(m_epoll_fd,m_sock_fd);
        m_sock_fd = -1;
        m_user_count--; // 关闭一个连接，将客户总数量-1
    }
}

bool http_conn::read() {
    printf("一次性读完数据\n");
    return true;
}
bool http_conn::write() {
    printf("一次性写完数据\n");
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求

    printf("parse request,create response\n");

    // 生成响应
}