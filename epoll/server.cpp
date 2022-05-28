#include <iostream>
#include <string>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cassert>
#include "../timer/timer_list.h"
#include "../http/http_conn.h"
#include "../pool/threadpool.h"
#include "server.h"

// 创建 epoll文件描述符
int Server::epoll_fd = 0;
int Server::pipe_fd[2];
static timer_list timer_list;  // 创建升序定时器链表类

// 添加文件描述符到epoll中
extern void add_fd(int epoll_fd,int fd,bool one_shot);
// 从epoll中移除文件描述符
extern void remove_fd(int epoll_fd,int fd);
// 设置非阻塞
extern int set_nonblocking(int fd);
// 修改文件描述符
extern void mod_fd(int epoll_fd,int fd,int ev);

Server::Server() = default;

Server::~Server() {
    close(listen_fd);
    close(epoll_fd);
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    delete[] users;
    delete[] users_timer;
    delete pool;
}


bool Server::init_server(const int& port, const std::string& sql_user, const std::string& sql_pwd,
                          const std::string& data_base, bool open_log, int log_level, int log_que_size)
                         {

    // 创建数据连接
    conn_pool = connection_pool::getInstance();
    conn_pool->init("localhost",sql_user,sql_pwd,data_base,3306,8);

    // 初始化数据库读取表
    users->init_mysql_result(conn_pool);

    // 创建线程池，初始化线程池
    pool = nullptr;
    try{
        pool = new ThreadPool<http_conn>(conn_pool);
    } catch (...) {
        return false;
    }

     if(!init_socket(port)) is_close = true;
     if(open_log) {
         Log::Instance()->init(log_level, "./log", ".log", log_que_size);
         if(is_close) {
             LOG_ERROR("========== Server init error!==========");
             return false;
         }
         else {
             LOG_INFO("========== Server init ==========");
         }
     }

    // 创建一个数组 用于保存所有的客户端信息
    users = new http_conn[MAX_FD];

    // 传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    addSig(SIGALRM);  // 计时器到期
    addSig(SIGTERM);  // 终止进程,即 kill

    // 连接资源数组
    users_timer = new client_data[MAX_FD];

    // 循环条件
    stop_server = false;

    // 超时标志 默认false
    timeout = false;

    // 开始计时
    alarm(TIMESLOT);
    return true;
}


bool Server::init_socket(const int& port) {
    // 创建用于监听的socket
    listen_fd = socket(PF_INET,SOCK_STREAM,0);
  //  assert(listen_fd >= 0);
    if(listen_fd < 0) {
        LOG_ERROR("create socket error!");
        return false;
    }

    int ret = 0;
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // 端口复用
    int reuse = 1;
    ret = setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof (reuse));
    if(ret < 0) {
        close(listen_fd);
        LOG_ERROR("set socket setsockopt error");
        return false;
    }

    // 绑定
    ret = bind(listen_fd,(sockaddr *)&address,sizeof (address));
    if(ret < 0) {
        close(listen_fd);
        LOG_ERROR("bind error!");
        return false;
    }
//    assert(ret >= 0);


    ret = listen(listen_fd,5);
//    assert(ret >= 0);
    if(ret < 0) {
        close(listen_fd);
        LOG_ERROR("listen error !");
        return false;
    }


    // 创建 epoll对象
    epoll_fd = epoll_create(5);
//    assert(epoll_fd != -1);
    if(epoll_fd == -1) {
        close(listen_fd);
        LOG_ERROR("create epoll error !");
        return false;
    }

    // 添加到epoll对象中
    add_fd(epoll_fd,listen_fd, false);
    http_conn::m_epoll_fd = epoll_fd;


    // 创建管道socket
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,pipe_fd);
//    assert(ret != -1);
    if(ret == -1) {
        close(listen_fd);
        LOG_ERROR("socketpair error !");
        return false;
    }

    // 设置管道写端为非阻塞
    set_nonblocking(pipe_fd[1]);
    // 设置管道为 LT非阻塞
    add_fd(epoll_fd,pipe_fd[0],false);
    LOG_INFO("server port: %d",port);
    return true;
}

// 设置信号
void Server::addSig(int sig) {
    struct sigaction sa{};
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);  // 设置信号集
    assert(sigaction(sig,&sa, nullptr) != -1);
    if(sigaction(sig,&sa, nullptr) == -1) LOG_ERROR("addsig error ");
}

// 定时器回调函数 删除非活动连接在socket上的注册事件，并关闭
void Server::cb_func(client_data *user_data) {
    // 删除非活动连接在socket上的注册事件
    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,user_data->socket_fd,nullptr);
    assert(user_data);
    // 关闭文件描述符
    close(user_data->socket_fd);
    // 减少连接数
    http_conn::m_user_count--;
}

// 信号处理函数
void Server::sig_handler(int sig) {
    //  保留原有的errno，函数执行后恢复，以保证函数的可重入性
    int save_errno = errno;
    int msg = sig;

    // 将信号写入管道，通知主循环
    send(pipe_fd[1],(char *)&msg,1,0);

    // 将原来的errno赋给当前的errno
    errno = save_errno;
}

// SIGALRM 信号的处理函数
void Server::timer_handler() {
    timer_list.tick();   //  调用升序定时器链表类的tick() 处理链表上到期的任务
    alarm(TIMESLOT); // 再次发出 SIGALRM 信号
}

void Server::handle_sig(const std::string &sig_msg) {
    for(auto &item : sig_msg) {     // 逐个处理接收的信号
        // 处理信号值对应的逻辑
        switch (item) {
            case SIGTERM:
            {
                stop_server = true;
            }
            case SIGALRM:    // 用timeout来标记有定时任务
            {                // 先不处理，因为定时任务优先级不高，优先处理其他事件
                timeout = true;
                break;
            }
        }
    }
}

void Server::run() {
    if(!is_close) LOG_INFO("========== Server start ==========");
    int ret = 0;
    // 创建epoll 事件数组
    epoll_event events[MAX_EVENT_NUMBER];

    while(!stop_server) {
        // 监测发生事件的文件描述符
        int number = epoll_wait(epoll_fd,events,MAX_EVENT_NUMBER,-1);
        if(number < 0 && (errno != EINTR)) {
//            printf("epoll failure\n");
            LOG_ERROR("epoll failure");
            break;
        }

        // 循环遍历事件数组
        for(int i = 0;i < number; ++i) {
            int sock_fd = events[i].data.fd;

            if(sock_fd == listen_fd) {
                // 有客户端连接进来
                struct sockaddr_in client_address{};
                socklen_t client_address_len = sizeof (client_address);
                int conn_fd = accept(listen_fd,(sockaddr*)&client_address,&client_address_len);

                if(conn_fd < 0) {
//                    printf( "errno is: %d\n", errno );
                    LOG_WARN("errno is: %d", errno);
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD ) {
                    // 目前连接数满了
                    LOG_WARN("Clients is full!");
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
                // 设置客户端活动时间
                timer->expire = cur + 3 * TIMESLOT;
                // 创建该连接对应的定时器   并初始化
                users_timer[conn_fd].timer = timer;
                // 将该定时器添加到链表中
                timer_list.add_timer(timer);


            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或错误等事件
                // users[sock_fd].close_conn();
                // 关闭连接 移除对应定时器
                auto *timer = users_timer[sock_fd].timer;
                cb_func(&users_timer[sock_fd]);

                if(timer)  timer_list.del_timer(timer);
            }
                //  管道读端对应文件描述符发生 读事件
            else if((sock_fd == pipe_fd[0]) && (events[i].events & EPOLLIN)){
                char buf[BUFFER_SIZE];
                // 从管道读端读出信号值，成功返回字节数，失败返回-1
                ret = recv(pipe_fd[0],buf,sizeof (buf),0);
                if(ret == -1 || ret == 0) {
                    continue;
                } else {
                    std::string sig_msg(buf);
                    handle_sig(sig_msg);
                }
            }

                // 处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN) {
                // 创建定时器临时变量，将该连接对应的定时器取出来
                auto *timer = users_timer[sock_fd].timer;
                // 读事件
                if(users[sock_fd].read()) {
                    LOG_INFO("deal with the client");
                    // 将时间放入请求队列
                    pool->append(users + sock_fd);
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 对其在链表上的位置进行调整
                    if(timer) {
                        time_t  cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                       // LOG_INFO("adjust timer once");
                    }
                } else {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sock_fd]);
                    if(timer) timer_list.del_timer(timer);
                }
            }
            else if(events[i].events & EPOLLOUT) {
                // 创建定时器临时变量，将该连接对应的定时器取出来
                auto *timer = users_timer[sock_fd].timer;
                // 写事件
                if(users[sock_fd].write()) {
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 对其在链表上的位置进行调整
                    LOG_INFO("send data to the client");
                    if(timer) {
                        time_t  cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                       // LOG_INFO("adjust timer once");
                    }
                } else {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sock_fd]);
                    if(timer) timer_list.del_timer(timer);
                    LOG_ERROR("Unexpected event");
                }
            }
        }
        // 处理定时器并不是收到信号后马上处理 完成读写事件后 再进行处理
        if(timeout) {
            timer_handler();
            timeout = false;
        }
    }
}



