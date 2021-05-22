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
#include "http_conn.h"
#include "threadpool.h"
#include "log.h"

#define SERVER_PORT 9999 
#define OPEN_FILES 10000 // 最大事件数
#define FD_LIMIT 65536 // 最大文件描述符
#define TIMESLOT 5

#define SYNLOG // 同步写日志
// #define ASYNLOG // 异步写日志

static int pipefd[2];
static int epollfd = 0;
static bool stop_server = false;
// 设置定时器相关参数
// 基于升序链表的定时器容器
static sort_timer_lst timer_lst;
// 超时标志
bool timeout = false;


extern void setnonblocking(int fd); 
extern void addfd(int epollfd, int fd, bool one_shot); 



// 定时处理任务
void timer_handler() {
    timer_lst.tick();
    // 因为一次alarm调用只会引起一次SIGALRM信号，索引要重新定时，以不断触发SIGALRM信号
    alarm(TIMESLOT);
}


void sig_handler(int sig_num) {
    // 保留原来的errno，保证函数的可重入性
    int save_errno = errno;
    write(pipefd[1], (char*)&sig_num, 1); // 将信号值写入管道，用于通知主函数中的主循环
    errno = save_errno;
}
void add_sig(int sig_num, void (handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));    
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);

    int ret = sigaction(sig_num, &sa, NULL);
    if (ret == -1) {
        perr_exit("add_sig error");
    }

}

// 定时器回调函数, 删除非活动连接
void cb_func(client_data *user_data) {
    // 从内核事件表删除事件
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    // 关闭文件描述符
    Close(user_data->sockfd);
    // 减少连接数
    Http_conn::m_user_count--;
    // printf("close fd: %d\n", user_data->sockfd);
    LOG_INFO("close fd: %d", user_data->sockfd);
    Log::get_instance()->flush();
}

// 向客户端发送错误信息
void show_error(int clientfd, const char *info) {
    printf("%s", info);
    send(clientfd, info, strlen(info), 0);
    close(clientfd);
}

int main(int argc, char *argv[]) {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); // 异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); // 同步日志模型
#endif

    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        exit(1);
    }

    // 创建数据库连接池
    Connection_pool *conn_pool = Connection_pool::get_instance();
    conn_pool->init("localhost", "root", "root", "learn", 3306, 8);

    // 线程池
   Threadpool<Http_conn> *pool = NULL;
   pool = new Threadpool<Http_conn>(conn_pool);
   if (pool == nullptr) {
       fprintf(stderr, "[%d: %s] create threading pool failed\n", __LINE__, __FILE__);
       return 1;
   }

    // 存储客户链接数据
   Http_conn* users = new Http_conn[FD_LIMIT];
   if (users == nullptr) {
       fprintf(stderr, "[%d: %s] create users buffer failed", __LINE__, __FILE__);
   }
   // 初始化数据库读取表
   users->init_mysql_result(conn_pool);

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
    addfd(epollfd, listenfd, false);
    Http_conn::m_epollfd = epollfd;

    // 统一事件源：处理信号的事件
    // 创建管道套接字
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    if (ret != 0) {
        perr_exit("socketpair error");
        exit(1);
    }
    // 管道写端为非阻塞：如果写端为阻塞状态，缓冲区满了时，write会阻塞，进一步增加信号处理函数的执行时间，为了防止这种情况，设置非阻塞
    setnonblocking(pipefd[1]);
    // 管道读端为ET模式，非阻塞，不使用EPOLLONESHOT
    addfd(epollfd, pipefd[0], false);

    // 设置信号处理函数
    add_sig(SIGHUP, sig_handler);
    add_sig(SIGCHLD, sig_handler);
    add_sig(SIGTERM, sig_handler);
    add_sig(SIGINT, sig_handler);
    add_sig(SIGALRM, sig_handler);
    add_sig(SIGPIPE, SIG_IGN);

    // 创建连接资源数组：存储每个用户与定时器有关的数据
    client_data *users_timer = new client_data[FD_LIMIT];
    // 每隔TIMESLOT时间触发SIGALARM信号
    alarm(TIMESLOT);

    while (!stop_server) {
        ret = epoll_wait(epollfd, events, OPEN_FILES, -1);
        if (ret < -1) {
            LOG_ERROR("%s", "epoll failure");
            perr_exit("epoll wait error");
        }
        // lt(events, ret, epollfd, listenfd);
        // et(events, ret, epollfd, listenfd);
        for (int i = 0; i < ret; ++i)
        {
            int clientfd = events[i].data.fd;
            if (clientfd == listenfd) {// 处理新的客户链接
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                clientfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addr_len);
                if (clientfd < 0) {
                    LOG_ERROR("%s: errno is %d", "accept error", errno);
                }
                if (Http_conn::m_user_count >= FD_LIMIT) { // 服务器无法接收新的连接
                    // 向客户端发送错误信息
                    show_error(clientfd, "Internal server busy");
                    // 服务端
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }

                users[clientfd].init(clientfd, client_addr);
                
                /* 
                创建定时器，设置回调函数与超时时间，然后绑定定时器与用户数据，
                最后将定时器添加到链表中
               */
                // 初始化该连接对应的连接资源
                users_timer[clientfd].address = client_addr;
                users_timer[clientfd].sockfd = clientfd;
                util_timer *timer = new util_timer;
                // 设置与定时器有关的连接资源
                timer->user_data = &users_timer[clientfd];
                timer->cb_func = cb_func;
                time_t cur = time(nullptr);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[clientfd].timer = timer;
                // 将该定时器添加到链表上
                timer_lst.add_timer(timer);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 处理异常事件：服务器关闭连接，移除对应的定时器
                util_timer *timer = users_timer[clientfd].timer;
                timer->cb_func(&users_timer[clientfd]);
                if (timer) {
                    timer_lst.del_timer(timer);
                }
            }
            else if (clientfd == pipefd[0] && (events[i].events & EPOLLIN)) { // 处理信号：管道读端文件描述符发生读事件
                char signals[1024];
                int ret = read(pipefd[0], signals, sizeof(signals));
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int j = 0; j < ret; ++j)
                    {
                        switch (signals[i])
                        {
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
            else if (events[i].events & EPOLLIN) { // 读事件：处理客户连接上接收到的数据
                util_timer *timer = users_timer[clientfd].timer;
                if (users[clientfd].read()) {
                    LOG_INFO("deal with the client (%s)", inet_ntoa(users[clientfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    // 检测到读事件，将事件放入请求队列
                    pool->append(users+clientfd);
                    /* 
                        从客户端中可以读取数据，调整相应连接的定时器，从而延迟该连接
                    */
                    if (timer) {
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else { // 对方关闭连接或者读取数据时出错，关闭连接
                    timer->cb_func(&users_timer[clientfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            else if (events[i].events & EPOLLOUT) { // EPOLLOUT：数据可写
                util_timer *timer = users_timer[clientfd].timer;
                if (users[clientfd].write()) {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[clientfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                   // 若有数据传输，将定时器往后延迟3个单位
                   // 并对新的定时器在链表上的位置进行调整
                   if (timer) {
                       time_t cur = time(NULL);
                       timer->expire = cur + 3 * TIMESLOT;
                       LOG_INFO("%s", "adjust timer once");
                       Log::get_instance()->flush();
                       timer_lst.adjust_timer(timer);
                   } 
                }
                else {
                    // 服务器关闭连接：移除对应的定时器
                    timer->cb_func(&users_timer[clientfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        // 处理定时任务
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }

    Close(epollfd);
    Close(listenfd);
    Close(pipefd[0]);
    Close(pipefd[1]);
    delete [] users;
    delete[] users_timer;
    delete pool;

    return 0;
}