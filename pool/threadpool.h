#ifndef WEBSERVER_ThreadPool_H
#define WEBSERVER_ThreadPool_H

#include "sql_connection_pool.h"
#include <mutex>
#include <queue>
#include <condition_variable>
#include <iostream>

// 线程池类，将它定义为模板类是为了代码复用
template<typename T>
class ThreadPool {
public:
    // thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量
    explicit ThreadPool(connection_pool *conn_pool = nullptr,int thread_number = 8,int max_requests = 10000);

    bool append(T* request); // 添加任务接口

    ~ThreadPool();
private:

    int m_thread_num;             // 线程的数量
    int m_max_requests;           // 请求队列中最多允许的、等待处理的请求的数量
    std::queue<T* > m_work_que;   // 任务队列
    std::mutex m_mutex;           // 互斥量
    std::condition_variable cond; // 条件变量
    bool m_stop;                  // 是否结束线程
    connection_pool *m_conn_pool; // 数据库

private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行之
    static void *worker(void *arg);
    void run();
};

template<typename T>
ThreadPool<T>::ThreadPool(connection_pool *conn_pool,int thread_number, int max_requests):
        m_conn_pool(conn_pool),m_thread_num(thread_number),m_max_requests(max_requests),m_stop(false){

    if(m_thread_num <= 0 || m_thread_num > m_max_requests) {
        throw std::exception();
    }


    // 创建thread_number 个线程，并将他们设置为脱离线程
    for(int i = 0; i < m_thread_num; ++i) {
        pthread_t pid;  // 创建线程
        if(pthread_create(&pid, nullptr,worker,static_cast<void*>(this)) == 0) {
            std::cout << "create " << i + 1 << " thread" << std::endl;
            pthread_detach(pid); // 线程分离
        }
    }

}

template<typename T>
ThreadPool<T>::~ThreadPool<T>() {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    cond.notify_all(); // 通知所有线程停止
}

template<typename T>
bool ThreadPool<T>::append(T *request) {
    if(m_work_que.size() >= m_max_requests) {
        std::cout << "ThreadPool: Work queue is full" << std::endl;
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_work_que.template emplace(request);
    }
    cond.notify_one(); // 通知线程
    return true;
}

template< typename T >
void *ThreadPool<T>::worker(void *arg) {
    auto * pool = static_cast<ThreadPool* >(arg);
    pool->run();
    return nullptr;
}

template< typename T >
void ThreadPool<T>::run() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while(!m_stop) {
        cond.wait(lock);
        if(!m_work_que.empty()) {
            T* request = m_work_que.front(); // 取出第一个
            m_work_que.pop();
            connection_RAII mysql_con(&request->mysql,m_conn_pool);
            request->process();
        }
    }
}

#endif //WEBSERVER_ThreadPool_H