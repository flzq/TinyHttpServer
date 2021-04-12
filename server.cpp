#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "wrap.h"
#include "lst_timer.h"

#define SERVER_PORT 9999
#define OPEN_FILES 1024
#define BUFFER_SIZE 10 
#define FD_LIMIT 65535
#define TIMESLOT 5

static int pipefd[2];
static int epollfd = 0;
static bool stop_server = false;
static sort_timer_lst timer_lst;
client_data* users = new client_data[FD_LIMIT];
bool timeout = false;

void setnonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    if (flag < 0) {
        perr_exit("fcntl: get");
    }
    flag |= O_NONBLOCK;
    int ret = fcntl(fd, F_SETFL, flag);
    if (ret < 0) {
        perr_exit("fcntl: set");
    }
}

void addfd(int epollfd, int fd, bool enable_et) {
    struct epoll_event tmp_ep;
    char buf[BUFSIZ];
    tmp_ep.events = EPOLLIN;
    tmp_ep.data.fd = fd;
    if (enable_et) {
        tmp_ep.events |= EPOLLET;
    }
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &tmp_ep); 
    if (ret < 0) {
        sprintf(buf, "%d: epoll_ctl error", __LINE__);
        perr_exit(buf);
    }
    setnonblocking(fd);
}

void sig_handler(int sig_num) {
    // 保留原来的errno，保证函数的可重入性
    int save_errno = errno;
    write(pipefd[1], (char*)&sig_num, 1); // 将信号值写入管道，用于通知主函数中的主循环
    errno = save_errno;
}

// 定时处理任务
void timer_handler() {
    timer_lst.tick();
    // 因为一次alarm调用只会引起一次SIGALRM信号，索引要重新定时，以不断触发SIGALRM信号
    alarm(TIMESLOT);
}

void add_sig(int sig_num) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));    
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);

    int ret = sigaction(sig_num, &sa, NULL);
    if (ret == -1) {
        perr_exit("add_sig error");
    }

}

// 定时器回调函数, 删除非活动连接
void cb_func(client_data *user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    Close(user_data->sockfd);
    printf("close fd: %d\n", user_data->sockfd);
}

void lt(epoll_event *events, int num, int epollfd, int listenfd) {
    int clientfd;
    char buf[BUFFER_SIZE];
    for (int i = 0; i < num; ++i) {
        if (!(events[i].events & EPOLLIN)) {
            continue;
        }
        if (events[i].data.fd == listenfd) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            clientfd = Accept(listenfd, (struct sockaddr*)&client_addr, &client_addr_len); 
            printf("accept: %d", clientfd);
            addfd(epollfd, clientfd, false);
        }
        else {
            printf("event trigger once\n");
            memset(buf, '\0', BUFFER_SIZE);
            clientfd = events[i].data.fd;
            int ret = read(clientfd, buf, BUFFER_SIZE);
            if (ret < 0) {
                printf("close %d\n", clientfd);
                Close(clientfd);
                continue;
            }
            else if (ret == 0) {
                ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
                if (ret == -1)
                {
                    sprintf(buf, "%d: epoll_ctl error", __LINE__);
                    perr_exit(buf);
                }
                Close(clientfd);
            }
            else {
                printf("get %d bytes of content: %s\n", ret, buf); 
            }
        }
    }
}

void et(struct epoll_event *events, int num, int epollfd, int listenfd) {
    int clientfd;
    char buf[BUFFER_SIZE];
    for (int i = 0; i < num; ++i) {
        if (!(events[i].events & EPOLLIN)) {
            continue;
        }
        clientfd = events[i].data.fd;
        if (clientfd == listenfd) {
           struct sockaddr_in client_addr;
           socklen_t client_addr_len = sizeof(client_addr);
           clientfd = Accept(listenfd, (struct sockaddr*)&client_addr, &client_addr_len); 
           addfd(epollfd, clientfd, true);
           users[clientfd].address = client_addr;
           users[clientfd].sockfd = clientfd;
           /* 
                创建定时器，设置回调函数与超时时间，然后绑定定时器与用户数据，
                最后将定时器添加到链表中
           */
            util_timer *timer = new util_timer;
            timer->user_data = &users[clientfd];
            timer->cb_func = cb_func;
            time_t cur = time(nullptr);
            timer->expire = cur + 3 * TIMESLOT;
            users[clientfd].timer = timer;
            timer_lst.add_timer(timer);
        }
        else if (clientfd == pipefd[0]) {
           char signals[1024];
           int ret = read(pipefd[0], signals, sizeof(signals));
           if (ret == -1) {
               continue;
           }
           else if (ret == 0) {
               continue;
           }
           else {
               for (int j = 0; j < ret; ++j) {
                   switch(signals[i]) {
                        case SIGCHLD:
                        case SIGHUP:
                            continue;
                        case SIGALRM:
                            timeout = true;
                            break;
                        case SIGTERM:
                        case SIGINT:
                            stop_server = true;
                   }
               }
           }

        }
        else {
            printf("event trigger once\n");
            while (1) {
                memset(buf, '\0', BUFFER_SIZE);
                int ret = read(clientfd, buf, BUFFER_SIZE-1);
                util_timer *timer = users[clientfd].timer;
                if (ret < 0) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        printf("read later\n");
                        break;
                    }
                    // 发生错误，关闭连接，移除相应定时器
                    perror("read error");
                    cb_func(&users[clientfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                    
                    // 在使用定时器之前，读数据遇到错误时，服务器关闭连接的方式
                    // ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
                    // if (ret == -1) {
                    //     sprintf(buf, "%d: epoll_ctl error", __LINE__);
                    //     perr_exit(buf);
                    // }
                    // Close(clientfd);
                    // perror("read error");

                    break;
                }
                else if (ret == 0) { // 对方关闭连接，则服务器也关闭连接，并且移除定时器
                    printf("%d: client close\n", __LINE__);
                    cb_func(&users[clientfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                    
                    // 在使用定时器之前，对方关闭连接时，服务器关闭连接的方式
                    // ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
                    // if (ret == -1) {
                    //     sprintf(buf, "%d: epoll_ctl error", __LINE__);
                    //     perr_exit(buf);
                    // }
                    // Close(clientfd);

                    break;
                }
                else {
                   printf("get %d bytes of content: %s\n", ret, buf); 
                   /* 
                        从客服端中可以读取数据，调整相应连接的定时器，从而延迟该连接
                        被关闭的时间
                   */
                    if (timer) {
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_lst.adjust_timer(timer);
                    }
                }
            }
        }

    }
}

int main(int argc, char *argv[]) {

    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        exit(1);
    }

    int listenfd, clientfd, epollfd, ret;
    struct epoll_event tmp_ep, events[OPEN_FILES];
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));

    Bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    Listen(listenfd, 128);

    epollfd = epoll_create(OPEN_FILES);
    if (epollfd < 0) {
        perr_exit("epoll_create");
    }
    addfd(epollfd, listenfd, true);

    // 统一事件源：处理信号的事件
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    if (ret != 0) {
        perr_exit("socketpair error");
        exit(1);
    }
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], true);

    // 设置信号处理函数
    add_sig(SIGHUP);
    add_sig(SIGCHLD);
    add_sig(SIGTERM);
    add_sig(SIGINT);
    add_sig(SIGALRM);

    alarm(TIMESLOT);
    while (!stop_server) {
        ret = epoll_wait(epollfd, events, OPEN_FILES, -1);
        if (ret < -1) {
            perr_exit("epoll wait error");
        }
        // lt(events, ret, epollfd, listenfd);
        et(events, ret, epollfd, listenfd);

        // 处理定时任务
        if (timeout) {
            timer_handler();
            timeout = false;
        }

    }

    Close(listenfd);
    Close(pipefd[0]);
    Close(pipefd[1]);
    delete [] users;

    return 0;
}