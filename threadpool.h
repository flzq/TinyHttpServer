#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include "lock.h"

template <typename T>
class Threadpool {
public:
    /*
        thread_num：线程池中线程数量
        max_requests：请求队列中最多运行的、等待处理的请求的数量
    */
    Threadpool(int thread_num = 8, int max_requests = 10000);
    ~Threadpool();
    // 往请求队列中添加任务
    bool append(T *request);
private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num; // 线程池中的线程数
    int m_max_requests; // 请求队列中运行的最大请求数量
    pthreaad_t *m_threads; // 线程池数组，大小为 thread_num
    std::list<T*> m_workqueue; // 请求队列
    Locker m_queuelocker; // 保护请求队列的互斥锁
    Sem m_queuestat; // 是否有任务需要处理
    bool m_stop; // 是否结束线程
};

template <typename T>
Threadpool<T>::Threadpool(int thread_num, int max_requests) : 
    m_thread_num(thread_num), m_max_requests(max_requests), 
    m_stop(false), m_threads(nullptr) {

    if ( (m_thread_num <= 0) || (m_max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];
    if (m_threads == nullptr) {
        throw std::exception();
    }

    // 创建 m_thread_num 个线程
    for (int i = 0; i < m_thread_num; ++i) {
        printf("create the %dth thread\n", i);
        if (pthread_create(m_threads+i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        // 线程分离
        if (pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }

}

template <typename T>
Threadpool<T>::~Threadpool() {
    delete [] m_threads;
    m_stop = true;
}

template <typename T>
bool Threadpool<T>::append(T *request) {
    // 操作工作队列时要加锁
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *Threadpool<T>::worker(void *arg) {
    Threadpool<T> *pool = (Threadpool<T>*)arg;
    pool->run();
    return pool;
}

template <typename T>
void Threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        request->process();
    }
}


#endif