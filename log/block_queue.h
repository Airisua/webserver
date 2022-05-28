#ifndef WORKSPACE_BLOCK_QUEUE_H
#define WORKSPACE_BLOCK_QUEUE_H

#include <iostream>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <cassert>
using namespace std;

template <class T>
class block_queue
{
public:
    explicit block_queue(int max_size = 1000);
    ~block_queue();
    void clear();
    bool full();
    bool empty();
    void close();
    bool front();
    bool back();
    int size();
    int max_size();
    void push_back(const T &item);
    void push_front(const T &item);
    bool pop(T&);
    bool pop(T&, int);
    void flush();


private:

    std::deque<T> deq;
    std::mutex mtx;
    bool isClose;
    std::condition_variable cond_consumer;
    std::condition_variable cond_producer;
    int m_max_size{};
};

template<class T>
block_queue<T>::block_queue(int max_size) {
    assert(max_size > 0);
    isClose = false;
}

template<class T>
block_queue<T>::~block_queue() {
    {
        std::lock_guard<std::mutex> locker(mtx);
        deq.clear();
        isClose = true;
    }
    cond_producer.notify_all();
    cond_consumer.notify_all();
}

// 刷新
template<class T>
void block_queue<T>::flush() {
    cond_consumer.notify_one();
}

template<class T>
void block_queue<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx);
    deq.clear();
}

//判断队列是否满了
template<class T>
bool block_queue<T>::full() {
    std::lock_guard<std::mutex> locker(mtx);
    return deq.size() >= m_max_size;
}

//判断队列是否为空
template<class T>
bool block_queue<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx);
    return deq.empty();
}

template<class T>
void block_queue<T>::close() {
    {
        std::lock_guard<std::mutex> locker(mtx);
        deq.clear();
        isClose = true;
    }
    cond_consumer.notify_all();
    cond_producer.notify_all();
}

//返回队首元素
template<class T>
bool block_queue<T>::front() {
    std::lock_guard<std::mutex> locker(mtx);
    return deq.front();
}

//返回队尾元素
template<class T>
bool block_queue<T>::back() {
    std::lock_guard<std::mutex> locker(mtx);
    return deq.back();
}

template<class T>
int block_queue<T>::size() {
    std::lock_guard<std::mutex> locker(mtx);
    return deq.size();
}

template<class T>
int block_queue<T>::max_size() {
    std::lock_guard<std::mutex> locker(mtx);
    return m_max_size;
}


//往队列添加元素，需要将所有使用队列的线程先唤醒
//当有元素push进队列,相当于生产者生产了一个元素
template<class T>
void block_queue<T>::push_back(const T &item) {
    std::unique_lock<std::mutex> locker(mtx);
    while(deq.size() >= m_max_size) {
        cond_producer.wait(locker);
    }
    deq.push_back(item);
    cond_consumer.notify_one();
}

template<class T>
void block_queue<T>::push_front(const T &item) {
    std::unique_lock<std::mutex> locker(mtx);
    while(deq.size() > m_max_size) {
        cond_producer.wait(locker);
    }
    deq.push_front(item);
    cond_consumer.notify_one();
}


//pop时,如果当前队列没有元素,将会等待条件变量
template<class T>
bool block_queue<T>::pop(T &item) {
    std::unique_lock<std::mutex> locker(mtx);
    while(deq.empty()) {
        cond_consumer.wait(locker);
        if(isClose) return false;
    }
    item = deq.front();
    deq.pop_front();
    cond_producer.notify_one();
    return true;
}

//增加了超时处理
template<class T>
bool block_queue<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx);
    while(deq.empty()){
        if(cond_consumer.wait_for(locker, std::chrono::seconds(timeout))
           == std::cv_status::timeout){
            return false;
        }
        if(isClose){
            return false;
        }
    }
    item = deq.front();
    deq.pop_front();
    cond_producer.notify_one();
    return true;
}


#endif //WORKSPACE_BLOCK_QUEUE_H
