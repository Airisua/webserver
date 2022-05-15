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
#include <cassert>
#include "locker/locker.h"
#include "pool/threadpool.h"
#include "http/http_conn.h"
#include "timer/timer.h"
#include "pool/sql_connection_pool.h"

#define MAX_FD 65536 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数
#define TIMESLOT 5 // 最小超时单位

//设置定时器相关参数
static int pipe_fd[2];
static sort_timer_lst timer_lst;  // 定时器容器链表
static int epoll_fd = 0;

// 信号处理函数
void sig_handler(int sig) {
    // 中断后再次进入该函数，环境变量与之前相同
    int save_errno = errno;
    int msg = sig;

    // 将信号从管道写端写入
    send(pipe_fd[1],(char *)&msg,1,0);

    // 将原来的errno赋给当前的errno
    errno = save_errno;
}


// 设置信号
void addSig(int sig,void(handler)(int),bool restart = true) {
    struct sigaction sa{};
    memset( &sa, '\0', sizeof( sa ));
    // 信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if(restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);  // 设置信号集
    // sigaction(sig,&sa, nullptr); // 注册信号捕捉
    assert(sigaction(sig,&sa, nullptr) != -1);
}

// 定时处理任务,重新定时以不断触发SIGALRM信号
void timer_handler(){
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数 删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data* user_data) {
   // 删除非活动连接在socket上的注册事件
    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,user_data->socket_fd,nullptr);
    assert(user_data);
    // 关闭文件描述符
    close(user_data->socket_fd);
    // 减少连接数
    http_conn::m_user_count--;
}

// 添加文件描述符到epoll中
extern void add_fd(int epoll_fd,int fd,bool one_shot);
// 从epoll中移除文件描述符
extern void remove_fd(int epoll_fd,int fd);
// 设置非阻塞
extern int set_nonblocking(int fd);
// 修改文件描述符
//extern void mod_fd(int epoll_fd,int fd,int ev);
//extern void mod_fd(int epoll_fd,int fd,int ev);


int main(int argc,char* argv[]) {
    if(argc <= 1) {
        printf("usage: %s port_number\n",basename(argv[0]));
        return -1;
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行处理 忽略SIGPIPE信号
    addSig(SIGPIPE,SIG_IGN);

    // 创建数据库连接
    connection_pool *conn_pool = connection_pool::getInstance();

    conn_pool->init("localhost", "wlic", "WYJV58787", "test_db", 3306, 8);

    // 创建线程池，初始化线程池
    ThreadPool<http_conn>* pool = nullptr;
        try{
            pool = new ThreadPool<http_conn>(conn_pool);
        } catch (...) {
            return -1;
        }

     // 创建一个数组 用于保存所有的客户端信息
    auto users = new http_conn[MAX_FD];

     // 初始化数据库读取表
     users->init_mysql_result(conn_pool);

     // 创建用于监听的socket
     int listen_fd = socket(PF_INET,SOCK_STREAM,0);

     int ret = 0;
     struct sockaddr_in address{};
     address.sin_family = AF_INET;
     address.sin_addr.s_addr = INADDR_ANY;
     address.sin_port = htons(port);

     // 端口复用
     int reuse = 1;
     setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof (reuse));
     // 绑定
     // ret = bind(listen_fd, reinterpret_cast<const sockaddr *>(&address), sizeof (address));
     ret = bind(listen_fd,(sockaddr *)&address,sizeof (address));
     assert(ret >= 0);
     ret = listen(listen_fd,6);
     assert(ret >= 0);


     // 创建epoll对象 事件数组
     epoll_event events[MAX_EVENT_NUMBER];
     epoll_fd = epoll_create(5);
     // 添加到epoll对象中
    add_fd(epoll_fd,listen_fd, false);
    http_conn::m_epoll_fd = epoll_fd;


    // 创建管道socket
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,pipe_fd);
    assert(ret != -1);
    // 设置管道写端为非阻塞
    set_nonblocking(pipe_fd[1]);
    // 设置管道为 LT非阻塞
    add_fd(epoll_fd,pipe_fd[0],false);
    // 传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    addSig(SIGALRM,sig_handler,false);
    addSig(SIGTERM,sig_handler,false);

    // 连接资源数组
    auto* users_timer = new client_data[MAX_FD];

    // 循环条件
    bool stop_server = false;

    // 超时标志 默认false
    bool timeout = false;

    // 每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);

    while(!stop_server) {
        // 监测发生事件的文件描述符
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
                // int conn_fd = accept(listen_fd,(sockaddr*)&client_address,&client_address_len);

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

                // 初始化客户端对应的连接资源
                users_timer[conn_fd].address = client_address;
                users_timer[conn_fd].socket_fd = conn_fd;

                // 创建定时器的临时资源
                auto* timer = new util_timer;
                // 设置定时器对应的连接资源
                timer->user_data = &users_timer[conn_fd];
                // 设置回调函数
                timer->cb_func = cb_func;

                // 当前时间
                time_t cur = time(nullptr);
                // 设置超时时间
                timer->expire = cur + 3 * TIMESLOT;
                // 创建该连接对应的定时器   并初始化
                users_timer[conn_fd].timer = timer;
                // 将该定时器添加到链表中
                timer_lst.add_timer(timer);


            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或错误等事件
                // users[sock_fd].close_conn();
                // 关闭连接 移除对应定时器
                cb_func(&users_timer[sock_fd]);

                auto *timer = users_timer[sock_fd].timer;
                if(timer)  timer_lst.del_timer(timer);
            }
               //  管道读端对应文件描述符发生 读事件
            else if((sock_fd == pipe_fd[0]) && (events[i].events & EPOLLIN)){
                int sig;
                char signals[1024];

                // 从管道读端读出信号值，成功返回字节数，失败返回-1
                ret = recv(pipe_fd[0],signals,sizeof (signals),0);
                if(ret == -1 || ret == 0) {
                    continue;
                } else {
                    // 处理信号值对应的逻辑
                    for(int j = 0; j < ret; ++j) {
                        if(signals[j] == SIGALRM) {
                            timeout = true;     //接受到SIGALRM信号 timeout设为true
                            break;
                        }
                        if(signals[j] == SIGTERM) {
                            stop_server = true;
                        }
                    }
                }
            }

            // 处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN) {
                // 创建定时器临时变量，将该连接对应的定时器取出来
                auto *timer = users_timer[sock_fd].timer;
                // 读事件
                if(users[sock_fd].read()) {
                    // 将时间放入请求队列
                    pool->append(users + sock_fd);
                    // 若有数据传输，则将定时器往后延迟3个单位、
                    // 对其在链表上的位置进行调整
                    if(timer) {
                        time_t  cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sock_fd]);
                    if(timer) timer_lst.del_timer(timer);
                }
            }
//            else if(events[i].events & EPOLLIN) {
//                // 读事件  一次性把所有数据读完
//                if(users[sock_fd].read()){
//                    pool->append(users + sock_fd);
//                } else {
//                    users[sock_fd].close_conn();
//                }
//           }
//            else if(events[i].events & EPOLLOUT) {
//                // 写事件  一次性把所有数据写完
//                if(!users[sock_fd].write()) {
//                    users[sock_fd].close_conn();
//                }
//            }
            else if(events[i].events & EPOLLOUT) {
                // 创建定时器临时变量，将该连接对应的定时器取出来
                auto *timer = users_timer[sock_fd].timer;
                // 写事件
                if(users[sock_fd].write()) {
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 对其在链表上的位置进行调整
                    if(timer) {
                        time_t  cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sock_fd]);
                    if(timer) timer_lst.del_timer(timer);
                }
            }
        }
        // 处理定时器并不是收到信号后马上处理 完成读写事件后 再进行处理
        if(timeout) {
            timer_handler();
            timeout = false;
        }
    }

    close(listen_fd);
    close(epoll_fd);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    delete [] users;
    delete pool;
    return 0;
}
