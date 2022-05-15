#ifndef WORKSPACE_TIMER_TEST_H
#define WORKSPACE_TIMER_TEST_H

#include <ctime>
#include <netinet/in.h>

// 连接资源类需要用到定时器类
// 这里是向前声明
class util_timer;

// 连接资源
class client_data {
public:
    //客户端socket地址
    sockaddr_in address;
    // socket文件描述符
    int socket_fd;
    // 定时器
    util_timer* timer;
};

// 定时器类
class util_timer {
public:
    util_timer(int rot, int st):rotation(rot),time_slot(st),prev(nullptr),next(nullptr) {}

public:
    int rotation; // 记录定时器在时间轮上转多少圈后生效
    int time_slot; // 记录定时器属于时间轮上的哪个槽
    // 回调函数
    void (*cb_func)(client_data* ){}; // 任务回调函数，处理的客户数据，由定时器的执行者传递给回调函数
    client_data* user_data{}; // 用户数据
    util_timer* prev; // 指向前一个定时器
    util_timer* next; // 指向后一个定时器
};


#endif //WORKSPACE_TIMER_TEST_H
