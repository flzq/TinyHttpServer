#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

class Sem {
public:
    // 创建并初始化信号量
    Sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    Sem (int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    // 销毁信号量
    ~Sem() {
        sem_destroy(&m_sem);
    }
    // 等待信号量
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    // 增加信号量
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

class Locker {
public:
    // 构造函数中初始化互斥量
    Locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    // 析构函数销毁锁
    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class Cond {
public:
    Cond() {
        // if (pthread_mutex_init(&m_mutex, NULL) != 0) {
        //     throw std::exception();
        // }
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            // pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~Cond() {
        // pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *pmutex) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, pmutex);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif