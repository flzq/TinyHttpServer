#include "http_conn.h"

// 文件描述符设置非阻塞
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

// 内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot) {
    struct epoll_event tmp_ep;
    char buf[BUFSIZ];
    tmp_ep.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    tmp_ep.data.fd = fd;
    if (one_shot) {
        tmp_ep.events |= EPOLLONESHOT;
    }
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &tmp_ep); 
    if (ret < 0) {
        sprintf(buf, "%d: epoll_ctl error", __LINE__);
        perr_exit(buf);
    }
    setnonblocking(fd);
}

// 从内核事件表删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    Close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;

    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int Http_conn::m_epollfd = -1;
int Http_conn::m_user_count = 0;


// 初始化新的连接
void Http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // reuse
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

// 初始化新连接
void Http_conn::init() {
    // check_state 从分析请求行状态开始
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    
    // 请求行数据初始化
    m_method = GET;
    cgi = 0;
    m_url = nullptr;
    m_version = nullptr;   

    m_checked_idx = 0;
    m_read_idx = 0;
    m_start_line = 0; 

}

// 服务器端关闭连接
void Http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        m_sockfd = -1;
        m_user_count--;
    }
}


// 循环读取从客户端中传输的数据，直到无数据可读或者对方关闭连接
// 非阻塞ET模式下，需要一次性将数据读完
bool Http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    } 

    int num_read = 0;
    while (true) {
        // 非阻塞IO读取
        num_read = read(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx);
        if (num_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // 非阻塞IO读取，若还没有数据，则跳出
                break;
            }
            // 读取数据时发生错误，返回false，关闭连接
            return false;
        }
        else if (num_read == 0) { // 对方关闭连接，则服务器也关闭连接，并且移除定时器
            return false;
        }
        m_read_idx += num_read;
    }

    return true;
}


void Http_conn::process() {
    HTTP_CODE read_ret = process_read();
    // NO_REQUEST：请求不完整，需要继续接收客户端请求报文
    if (read_ret == NO_REQUEST) {
        // 注册并且监听读事件，设置EPOLLIN和EPOLLONESHOT
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    // 注册并且监听写事件：设置EPOLLOUT和EPOLLONESHOT
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


Http_conn::HTTP_CODE Http_conn::process_read() {
    // 初始化从状态机状态，HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // parse_line：从状态机，负责读取报文的一行
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
            || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line(); // 获取一行的起始地址
        m_start_line = m_checked_idx; // 新的一行的下标

        // 主状态机的三种状态转移逻辑
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: { // 解析请求行
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: { // 解析请求头
                ret = parse_request_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: { // 解析消息体
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }

    // 还没有解析完HTTP请求
    return NO_REQUEST;
}

Http_conn::LINE_STATUS Http_conn::parse_line() {
    /*
        HTTP报文中，每一行的数据由\r\n作为结束字符
        从状态机通过\r\n 判断一行读取完成了
    */
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        // 如果当前字符是 '\r'，则有可能读取到完整的行
        if (m_read_buf[m_checked_idx] == '\r') {
            // 如果已经到达了读缓冲区的结尾，则要继续接收客户端数据
            if (m_checked_idx + 1 == m_read_idx) {
                break;
            }
            else if (m_read_buf[m_checked_idx+1] == '\n') { // 下一个字符是 \n，将 \r\n 改为 \0\0
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果都不符合，返回语法错误
            return LINE_BAD;
        }
        // 如果当前字符是 \n，也有可能读取到完整行
        // 一般是上次读取到\r就到读缓冲区末尾了，没有接收完整
        else if (m_read_buf[m_checked_idx] == '\n') { 
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    // 没有找到\r\n，继续接收客户端数据
    return LINE_OPEN;
}

Http_conn::HTTP_CODE Http_conn::parse_request_line(char *text) {
    // 请求行中的字段通过 \t 或者空格进行分隔
    m_url = strpbrk(text, " \t");
    // 如果没有空格或者\t，则报文格式有错误
    if (m_url == nullptr) {
        return BAD_REQUEST;
    }

    // 将该位置改为 \0，用于将前面数据取出
    *m_url = '\0';
    m_url++;

    // 取出第一部分数据，与 GET 或者 POST比较，确定请求方式
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }

    // 指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");

    // 判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    if (m_version == nullptr) {
        return BAD_REQUEST;
    }
    *m_version = '\0';
    ++m_version;
    // 只支持 HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 对请求资源的地址进行处理
    // 若请求资源带有 http://，则去掉该部分
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    // 若请求资源带有 https://，则去掉该部分
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // 排除上述情况后，若请求资源不是以 / 开始
    if (m_url == nullptr || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    // 当url为 / 时，显示欢迎页面
    if (strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }
    
    // 请求完毕，主状态机中对请求行处理转移到对请求头处理
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}