#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include "locker/locker.h"
#include "threadpool.h"
#include "http/http_conn.h"

#define MAX_FD 65535 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数

// 信号处理
void addSig(int sig,void(handler)(int)) {
    struct sigaction sa{};
    memset( &sa, '\0', sizeof( sa ));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);  // 设置信号集
    sigaction(sig,&sa, nullptr); // 注册信号捕捉
}

// 添加文件描述符到epoll中
extern void add_fd(int epoll_fd,int fd,bool one_shot);
// 从epoll中移除文件描述符
extern void remove_fd(int epoll_fd,int fd);
// 修改文件描述符
extern void mod_fd(int epoll_fd,int fd,int ev);
extern void mod_fd(int epoll_fd,int fd,int ev);

int main(int argc,char* argv[]) {

    if(argc <= 1) {
        printf("usage: %s port_number\n",basename(argv[0]));
        return -1;
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行处理
    addSig(SIGPIPE,SIG_IGN);

    // 创建线程池，初始化线程池
    ThreadPool<http_conn>* pool = nullptr;
        try{
            pool = new ThreadPool<http_conn>;
        } catch (...) {
            return -1;
        }

     // 创建一个数组 用于保存所有的客户端信息
    auto* users = new http_conn[MAX_FD];

     // 创建用于监听的socket
     int listen_fd = socket(PF_INET,SOCK_STREAM,0);

     //int ret = 0;
     struct sockaddr_in address{};
     address.sin_family = AF_INET;
     address.sin_addr.s_addr = INADDR_ANY;
     address.sin_port = htons(port);

     // 端口复用
     int reuse = 1;
     setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof (reuse));
     // 绑定
     bind(listen_fd, reinterpret_cast<const sockaddr *>(&address), sizeof (address));
     //ret = bind(listen_fd,(sockaddr *)&address,sizeof (address));
     listen(listen_fd,6);

     // 创建epoll对象 事件数组
     epoll_event events[MAX_EVENT_NUMBER];
     int epoll_fd = epoll_create(5);
     // 添加到epoll对象中
    add_fd(epoll_fd,listen_fd, false);
    http_conn::m_epoll_fd = epoll_fd;

    while(true) {
        int number = epoll_wait(epoll_fd,events,MAX_EVENT_NUMBER,-1);
        if(number < 0 && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        // 循环遍历数组
        for(int i = 0;i < number; ++i) {
            int sock_fd = events[i].data.fd;

            if(sock_fd == listen_fd) {
                // 有客户端连接进来
                struct sockaddr_in client_address{};
                socklen_t client_address_len = sizeof (client_address);
                int conn_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_address), &client_address_len);
                //int conn_fd = accept(listen_fd,(sockaddr*)&client_address,&client_address_len);

                if(conn_fd < 0) {
                    printf( "errno is: %d\n", errno );
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD ) {
                    // 目前连接数满了
                    // 这里可以给客户端一个信息 ： 服务器正忙
                    close(conn_fd);
                    continue;
                }

                // 将新的客户的信息初始化，放到数组中
                users[conn_fd].init(conn_fd,client_address);

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或错误等事件
                users[sock_fd].close_conn();
            }else if(events[i].events & EPOLLIN) {
                // 读事件  一次性把所有数据读完
                if(users[sock_fd].read()){
                    pool->append(users + sock_fd);
                } else {
                    users[sock_fd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT) {
                // 写事件  一次性把所有数据写完
                if(!users[sock_fd].write()) {
                    users[sock_fd].close_conn();
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);
    delete [] users;
    delete pool;
    return 0;
}
