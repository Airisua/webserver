#ifndef WORKSPACE_SERVER_H
#define WORKSPACE_SERVER_H

#include <iostream>
#include <string>
#include "../timer/timer_list.h"
#include "../http/http_conn.h"
#include "../pool/threadpool.h"
#include "../pool/sql_connection_pool.h"
#include "../log/log.h"

#define MAX_FD 65536 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数

class Server{
public:
    explicit Server();
    ~Server();
    bool init_server(const int& port, const std::string& sql_user, const std::string& sql_pwd, const std::string& data_base,
                    bool open_log, int log_level, int log_que_size);
    void run();

private:
    bool is_close = false;
    static int epoll_fd;
    connection_pool *conn_pool{};
    int listen_fd = 0;   // 用于监听的 socket
    http_conn* users{};
    client_data* users_timer{};
    bool timeout = false;
    bool stop_server = true;
    ThreadPool<http_conn>* pool = nullptr;
    //设置定时器相关参数
    static int pipe_fd[2];  // 信号通信管道

private:

    bool init_socket(const int& port);
    static void sig_handler(int sig); // 信号处理函数
    static void addSig(int sig);
    static void timer_handler();      // SIGALRM 信号的处理函数
    static void cb_func(client_data* user_data);   // 定时器回调函数 删除非活动连接在socket上的注册事件，并关闭
    void handle_sig(const std::string &sig_msg);

};

#endif //WORKSPACE_SERVER_H
