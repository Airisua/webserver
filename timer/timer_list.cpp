#include "timer_list.h"
#include <ctime>

timer_list::timer_list() = default;

timer_list::~timer_list() {
    m_timer_list.clear();
}

void timer_list::add_timer(util_timer *timer) {  // 将定时器添加到链表
    if(!timer) return;
    else {
        auto item = m_timer_list.begin();
        while(item != m_timer_list.end()) {
            if(timer->expire < (*item)->expire) {
                m_timer_list.insert(item,timer);
                return;
            }
            ++item;
        }
        m_timer_list.emplace_back(timer);
    }
}

void timer_list::del_timer(util_timer *timer) {  // 将定时器从链表删除
    if (!timer) return;
    else {
        auto item = m_timer_list.begin();
        while (item != m_timer_list.end()) {
            if (timer == *item) {
                m_timer_list.erase(item);
                m_timer_list.remove(timer);
                return;
            }
            item++;
        }
    }
}

void timer_list::adjust_timer(util_timer *timer) { // 调整定时器在链表中的位置
    del_timer(timer);
    add_timer(timer);
}

void timer_list::tick() {  // SIGALRM信号触发，处理链表上到期的任务
    if (m_timer_list.empty()) return;
    time_t cur = time(nullptr);

    //检测当前定时器链表中到期的任务。
    while (!m_timer_list.empty()) {
        util_timer* temp = m_timer_list.front();
        if (cur < temp->expire) break;
        temp->cb_func(temp->user_data);
        m_timer_list.pop_front();
    }
}