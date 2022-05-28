#ifndef WORKSPACE_TIMER_LIST_H
#define WORKSPACE_TIMER_LIST_H

#include <iostream>
#include <functional>
#include <netinet/in.h>
#include <list>

#define BUFFER_SIZE 64
#define TIMESLOT 10 // 最小超时单位

// 这里是向前声明
class util_timer;

// 客户端数据
class client_data{
public:
    sockaddr_in address; //socket地址
    int socket_fd; //socket文件描述符
    char buf[BUFFER_SIZE]; //数据缓存区
    util_timer* timer; //定时器
};

// 定时器类
class util_timer{
public:
    time_t expire; //任务超时时间(绝对时间)
    std::function<void(client_data*)> cb_func; //回调函数
    client_data* user_data; //用户数据
};


class timer_list{
public:
    explicit timer_list();
    ~timer_list();

public:
    void add_timer(util_timer* timer); //添加定时器
    void del_timer(util_timer* timer); // 删除定时器
    void adjust_timer(util_timer* timer); // 调整定时器
    void tick(); //处理链表上到期的任务

private:
    std::list<util_timer* > m_timer_list; // 定时器链表
};

#endif //WORKSPACE_TIMER_LIST_H
