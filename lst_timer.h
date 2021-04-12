#ifndef LST_TIMER_H
#define LST_TIMER_H
#include <time.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 64
class util_timer;
struct client_data {
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer *timer;
};

class util_timer {
public:
    util_timer() : prev(nullptr), next(nullptr) {}
    time_t expire;
    void (*cb_func) (client_data *);
    client_data *user_data;
    util_timer *prev, *next;
};

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

        if (head == nullptr) {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
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
        // 被调整的定时器为头结点
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer);
        }
        else { // 被调整的定时器不是头结点
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
        time_t cur = time(nullptr);
        util_timer *tmp = head;
        // 处理每个定时器任务，直到遇到一个尚未到期的定时器
        while (tmp) {
            if (cur < tmp->expire) {
                break;
            }
            tmp->cb_func(tmp->user_data);
            // 执行完定时器任务后
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