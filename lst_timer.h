#ifndef LST_TIMER_H
#define LST_TIMER_H
#include <time.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 64
class util_timer;
struct client_data {
    // 客户端地址
    struct sockaddr_in address;
    // 与客户的连接的文件描述符
    int sockfd;
    char buf[BUFFER_SIZE];
    // 相应定时器
    util_timer *timer;
};

class util_timer {
public:
    util_timer() : prev(nullptr), next(nullptr) {}
    // 超时时间
    time_t expire;
    // 回调函数
    void (*cb_func) (client_data *);
    // 连接资源
    client_data *user_data;
    util_timer *prev, *next;
};


// 带头尾指针的升序双向链表
class sort_timer_lst {
public:
    sort_timer_lst () : head(nullptr), tail(nullptr) {}
    ~sort_timer_lst () {
        util_timer *tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    // 添加计时器
    void add_timer(util_timer *timer) {
        if (timer == nullptr) {
            return;
        }
        // 链表中没有节点，直接插入
        if (head == nullptr) {
            head = tail = timer;
            return;
        }
        // 待插入的节点比链表中第一个节点到期时间早，插入头部
        if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        // 将定时器按照升序插入
        add_timer(timer, head);
    }

    // 调整某个定时器：只考虑定时器时间延长的情况
    void adjust_timer(util_timer *timer) {
        if (timer == nullptr) {
            return;
        }
        util_timer *tmp = timer->next;
        /* 
            如果被调整的定时器位于链表尾部，
            或者该定时器的超时值仍然小于下一个定时器的超时值，则不用调整
        */
        if (tmp == nullptr || (timer->expire < tmp->expire)) {
            return;
        }
        // 被调整的定时器为头结点，将定时器从链表中取出，重新插入链表
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer);
        }
        else { // 被调整的定时器不是头结点，将定时器从链表中取出，重新插入链表
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            timer->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer, tmp);
        }
    }

    // 删除目标定时器
    void del_timer(util_timer *timer) {
        if (timer == nullptr) {
            return;
        }
        // 当链表中只有一个定时器时
        if (head != nullptr && head == tail) {
            delete timer;
            head = tail = nullptr;
            return;
        }
        // 当链表中至少有两个定时器，且待删除节点为链表头结点时
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        // 当链表中至少有两个定时器，且待删除节点为链表尾结点时
        if (timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return; 
        }
        // 如果目标定时器位于链表内部时
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    
    /*
        SIGALRM 信号每次触发时则在信号处理函数中执行一次tick函数
    */
    void tick() {
        if (head == nullptr) {
            return;
        }
        printf("timer tick\n");
        // 获取当前时间
        time_t cur = time(nullptr);
        util_timer *tmp = head;
        // 处理每个定时器任务，直到遇到一个尚未到期的定时器
        while (tmp) {
            // 当前时间小于定时器超时时间，表示当前及其后面的定时器没有到期
            if (cur < tmp->expire) {
                break;
            }

            // 当前定时器到期，调用回调函数，执行定时事件
            tmp->cb_func(tmp->user_data);
            // 执行完定时器任务后，将该定时器从链表中删除，并重新设置头结点
            head = tmp->next;
            if (head) {
                head->prev = nullptr;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head) {
        util_timer *prev = lst_head;
        util_timer *cur = prev->next;
        while (cur)
        {
            if (timer->expire < cur->expire) {
                prev->next = timer;
                timer->prev = prev;
                timer->next = cur;
                cur->prev = timer;
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        // 遍历完链表后，没有找到超时时间大于timer的节点，则将timer插入到链表尾部
        if (cur == nullptr) {
            prev->next = timer;
            timer->prev = prev;
            tail = timer;
        }
    }

    util_timer *head, *tail; // 指向链表头节点和尾节点；

};


#endif