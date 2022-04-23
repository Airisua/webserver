
#ifndef WEBSERVER_ThreadPool_H
#define WEBSERVER_ThreadPool_H

#include "../locker/locker.h"
#include <list>
#include <cstdio>

// 线程池类，将它定义为模板类是为了代码复用
template<typename T>
class ThreadPool {
public:
    // thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量
    explicit ThreadPool(int thread_number = 10,int max_requests = 10000);

    bool append(T* request);

    ~ThreadPool();
private:
    // 线程的数量
    int m_thread_num;

    // 描述线程池的数组,大小为 m_thread_num
    pthread_t* m_threads;

    // 请求队列中最多允许的、等待处理的请求的数量
    int m_max_requests;

    // 请求队列
    std::list<T*> m_work_que;

    // 保护请求队列的互斥锁
    locker m_que_locker;

    // 是否有任务需要处理
    sem m_que_stat;

    // 是否结束线程
    bool m_stop;

private:
    static void *worker(void *arg);
    void run();
};

template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests):
m_thread_num(thread_number),m_max_requests(max_requests),m_threads(nullptr),m_stop(false){

    if(thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程
    for(int i = 0; i < thread_number; ++i) {
        printf("create the %dth thread\n",i);
        if(pthread_create(m_threads+i, nullptr,worker,this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        // 线程分离
        if(pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool<T>() {
    delete[] m_threads;
    this->m_stop = true;
}

template<typename T>
bool ThreadPool<T>::append(T *request) {
    // 操作工作队列时要加锁，因为它被所有线程共享
    m_que_locker.lock();
    // 超过的允许的最大请求数
    if(m_work_que.size() >= m_max_requests) {
        m_que_locker.unlock();
        return false;
    }
    m_work_que.push_back(request);
    m_que_locker.unlock();
    m_que_stat.post(); // 记得信号量加 1
    return true;
}

template< typename T >
void *ThreadPool<T>::worker(void *arg) {
    auto * pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template< typename T >
void ThreadPool<T>::run() {
    while(!m_stop) {
        m_que_stat.wait();  // 信号量 -1 没有数据时阻塞
        m_que_locker.lock();
        T* request = m_work_que.front(); // 取出第一个
        m_work_que.pop_front();
        m_que_locker.unlock();
        request->process();
    }
}

#endif //WEBSERVER_ThreadPool_H
