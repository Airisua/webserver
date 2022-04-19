#ifndef WORKSPACE_TIMER_H
#define WORKSPACE_TIMER_H

#include <ctime>

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
    util_timer():prev(nullptr),next(nullptr){}

public:
    // 超时时间
    time_t expire{};
    // 回调函数
    void (*cb_func)(client_data* ){}; // 任务回调函数，处理的客户数据，由定时器的执行者传递给回调函数
    client_data* user_data{}; // 连接资源
    util_timer* prev; // 指向前一个定时器
    util_timer* next; // 指向后一个定时器
};

// 定时器容器类  双向链表 为每个连接创建定时器 并按照超时时间排序
// 定时器到期则将其从链表中删除
class sort_timer_lst {
public:
    sort_timer_lst():head(nullptr),tail(nullptr){}
    // 删除链表
    ~sort_timer_lst(){
        util_timer* temp = head;
        while(temp) {
            head = temp->next;
            delete temp;
            temp = head;
        }
    }

    // 添加定时器 内部调用私有成员add_timer
    void add_timer(util_timer* timer) {
        if(!timer) {
            return;
        }
        if(!head) {
            head = tail = timer;
            return;
        }
        // 如果新定时器超时时间小于头结点
        // 则将新定时器作为头结点
        if(timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        // 否则掉用私有成员 add_timer调整内部结构
        add_timer(timer,head);
    }

    // 调整定时器 任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer* timer) {
        if(!timer)  return;

        // 被调整的定时器在链表尾部
        // 或者 该定时器超时时间仍然小于下一个定时器  不调整
        if(!timer->next || timer->expire < timer->next->expire)
            return;

        // 待调整定时器在头部 则将其取出 重新插入
        if(timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer,head);
        }
        // 带调整定时器在内部 将其取出 重新插入
        else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer,timer->next);
        }
    }

    // 删除定时器
    void del_timer(util_timer* timer) {
        if(!timer) return;

        // 整个链表中只有一个定时器时
        if((timer == head) && (timer == tail)) {
            delete timer;
            head = nullptr;
            tail = nullptr;
            return;
        }

        // 带删除定时器为头结点时
        if(timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }

        // 待删除定时器为尾结点时
        if(timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }

        // 带删除定时器在链表内部时
        else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
            return;
        }
    }

    // 定时任务处理函数
    // 从头结点开始依次处理每个定时器，直到遇到尚未到期的定时器
    void tick() {
        if(!head) return;
        // 获取当前时间
        time_t cur = time(nullptr);
        util_timer* temp = head;

        // 遍历定时器链表
        while(temp) {
            // 当前时间小于定时器的超时时间，后面的定时器也没有到期
            if(cur < temp->expire) return;

            // 当前定时器到期，则调用回调函数，执行定时事件
            temp->cb_func(temp->user_data);

            // 将处理后的定时器从链表容器中删除，并重置头结点
            head = temp->next;
            if(head) head->prev = nullptr;
            delete temp;
            temp = head;
        }
    }


private:
    // 私有成员，被公有成员add_timer和adjust_time调用
    // 主要用于调整链表内部结点
    void add_timer(util_timer* timer,util_timer* lst_head) {
        util_timer* prev = lst_head;
        util_timer* temp = prev->next;

        // 遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置
        while(temp) {
            if(timer->expire < temp->expire) {
                prev->next = timer;
                timer->next = temp;
                temp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = temp;
            temp = temp->next;
        }

        // 遍历完发现定时器需要放置尾结点
        if(!temp) {
            prev->next = timer;
            timer->next = nullptr;
            timer->prev = prev;
            tail = timer;
        }
    }
private:
    // 头尾结点
    util_timer* head;
    util_timer* tail;
};
#endif //WORKSPACE_TIMER_H
