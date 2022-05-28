
#ifndef WEBSERVER_LOCKER_H
#define WEBSERVER_LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class locker {
public:
    locker(){
        if(pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception();
        }
    }
    // 上锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    // 解锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获得锁
    pthread_mutex_t *get() {
        return &m_mutex;
    }

    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
private:
    pthread_mutex_t m_mutex{};
};

// 条件变量类
class cond {
public:
    cond() {
        if(pthread_cond_init(&m_cond, nullptr) != 0) {
            throw std::exception();
        }
    }
    // 等待，调用函数，线程会阻塞
    bool wait(pthread_mutex_t &m_mutex) {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond,&m_mutex);
        return ret == 0;
    }

    // 等待多长时间，调用了这个函数，线程会阻塞，直到指定的时间结束
    bool timeWait(pthread_mutex_t *m_mutex,const struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond,m_mutex,&t);
        return ret == 0;
    }

    // 唤醒一个或者多个等待的线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 唤醒所有的等待的线程
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
private:
    pthread_cond_t m_cond{};
};

// 信号量类
class sem{
public:
    sem() {
        if(sem_init(&m_sem,0,0) != 0) {
            throw std::exception();
        }
    }

    explicit sem(int num) {
        if(sem_init(&m_sem,0,num) != 0) {
            throw std::exception();
        }
    }

    // 等待信号量
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    // 增加信号量 给信号量的值加上一个“1”
    bool post() {
        return sem_post(&m_sem) == 0;
    }

    ~sem() {
        sem_destroy(&m_sem);
    }
private:
    sem_t m_sem{};
};

#endif //WEBSERVER_LOCKER_H