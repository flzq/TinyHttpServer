#include "http_conn.h"


// 网站根目录
const char *web_root = "/home/";

// 定义HTTP响应的一些状态
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

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
    
    // 请求行数据初始化
    m_method = GET;
    cgi = 0;
    m_url = nullptr;
    m_version = nullptr;   
    m_linger = false;

    // 请求头中数据初始化
    m_content_length = 0;

    // 缓存区相关数据初始化
    m_checked_idx = 0;
    m_read_idx = 0;
    m_start_line = 0; 
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);

    // 客户端请求资源相关数据初始化
    memset(m_real_file, '\0', FILENAME_LEN);

    // 向客户端写入数据初始化
    bytes_to_send = 0;
    m_iv_count = 0;
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
        num_read = ::read(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx);
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
    // 解析请求报文
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


Http_conn::HTTP_CODE Http_conn::parse_request_headers(char *text) {
    // 判断是请求头部分还是空行部分
    if (text[0] == '\0') {
        if (m_content_length != 0) { // POST请求，且消息体中有内容，解析消息体解析
           m_check_state = CHECK_STATE_CONTENT;
           return NO_REQUEST; 
        }
        return GET_REQUEST;
    }
    // 解析请求头部字段
    else if (strncasecmp(text, "Connection:", 11) == 0) { // 解析连接字段
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            // 如果是长连接，则将linger标志设置为true
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
}

Http_conn::HTTP_CODE Http_conn::parse_content(char *text) {
   // 判断读缓冲区中是否读取了完整的消息体
   if (m_read_idx >= (m_content_length+m_checked_idx)) {
       text[m_content_length] = '\0';
       m_string = text;

       return GET_REQUEST;
   } 
   return NO_REQUEST;
}


Http_conn::HTTP_CODE Http_conn::do_request() {
    // 初始化 m_real_file 为网站根目录
    strcpy(m_real_file, web_root);
    int len = strlen(m_real_file);
    // 找到 m_url 中 / 的位置
    const char *p = strrchr(m_url, '/');

    // 处理cgi，实现登录和注册校验
    if (cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')) {
        // 根据标志判断是登录检测还是注册检测

        // 同步线程登录校验
    }

    // 如果请求资源为 /0，表示跳转注册界面，POST请求
    if (*(p+1) == '0') {
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/register.html");

        // 将网站目录和 /register.html 进行拼接，更新到 m_real_file 中
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } 
    else if (*(p+1) == '1') { // 如果请求资源为1，跳转登录界面，POST请求
         char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/log.html");

        // 将网站目录和 /log.html 进行拼接，更新到 m_real_file 中
        strncpy(m_real_file+len, m_url_real, strlen(m_url_real));

        free(m_url_real);      
    }
    else if (*(p + 1) == '5') { // 请求资源为图片，POST请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } 
    else if (*(p + 1) == '6') { // 请求视频，POST请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7') { // 跳转到关注页面，POST请求
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else // 都不符合，跳转到欢迎界面，GET请求
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过 stat 获取资源信息，成功则将信息更新到 m_file_stat，失败返回 NO_RESOURCE
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    // 判断文件权限，是否可读
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 判断文件类型，如果是目录，返回 BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    // 以只读方式获取文件描述符，通过 mmap 将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // 表示请求的文件存在，且可以访问
    return FILE_REQUEST;
}

bool Http_conn::process_write(Http_conn::HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR: { // 内部错误，500
            // 状态行
            add_status_line(500, error_500_title);
            // 消息报头
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: { // 报文语法有误，404
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: { //资源没有访问权限，403
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST: { //文件存在，200
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) { // 如果请求的资源存在
                add_headers(m_file_stat.st_size);
                // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                // 发送的全部信息为响应报文头部信息和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else { // 如果请求的资源大小为0，则返回空白 html 文件
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }

    // 除了FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// HTTP响应时使用的一些函数
bool add_response(const char *format, ...) {

}
bool add_content(const char *content);
bool add_status_line(int status, const char *title); // 添加状态行
bool add_headers(int content_length);                // 添加消息报头，内部调用 add_content_length 和 add_linger
bool add_content_type();
bool add_content_length(int content_length);
bool add_linger();
bool add_blank_line(); // 添加空行
bool Http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}



















