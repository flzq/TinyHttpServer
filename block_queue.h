/*
    循环数组实现阻塞队列
    基于条件变量和互斥锁实现生产者消费者模型
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <stdlib.h>
#include "lock.h"

template <typename T>
class Block_queue {
public:
    Block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size = max_size;
        m_size = 0;
        m_front = m_back = -1;
        m_array = new T[max_size];
    }
    ~Block_queue() {
        m_mutex.lock();
        if (m_array != nullptr) {
            delete [] m_array;
        }
        m_mutex.unlock();
    }

    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = m_back = -1;
        m_mutex.unlock();
    }

    // 判断队列是否满了
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value) {
        m_mutex.lock();

        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }

        value = m_array[m_front];

        m_mutex.unlock();

        return true;
    }
    // 返回队尾元素
    bool back(T &value) {
        m_mutex.lock();

        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }

        value = m_array[m_back];

        m_mutex.unlock();
        return true;
    }

    // 返回队列元素数量
    int size() {
        int tmp;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();

        return tmp;
    }
    // 返回队列容量
    int capacity() {
        int tmp;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    // 往队列中添加元素：生产者
    bool push(const T &item) {
        m_mutex.lock();
        // 队列中元素已满，唤醒消费者
        if (m_size >= m_max_size) {
            m_cond.signal();
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_mutex.unlock();
        m_cond.signal();
        return true;
    }
    // pop元素：消费者
    bool pop(T &item) {
        m_mutex.lock();
        // 多个消费者时，使用while
        while (m_size <= 0) {
            m_cond.wait(m_mutex.get());
        }
        m_front = (m_front + 1) % m_max_size;
        m_array[m_front] = item;
        m_size--;

        m_mutex.unlock();
        return true;
    }

private:
    Locker m_mutex;
    Cond m_cond;

    T *m_array;
    int m_max_size;
    int m_size;
    int m_front;
    int m_back;
};


#endif