#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <errno.h>
#include "wrap.h"

class Http_conn {
public:
    static const int FILENAME_LEN = 200;
    // 读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 支持的请求方法
    enum METHOD { 
        GET = 0, 
        POST, 
        HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH 
    };
    // 主状态机状态
    enum CHECK_STATE { 
        CHECK_STATE_REQUESTLINE = 0, // 解析请求行
        CHECK_STATE_HEADER, // 解析请求头 
        CHECK_STATE_CONTENT // 解析消息体，仅用于 POST 请求 
    };
    // Http 请求状态：服务器处理HTTP请求的结果
    enum HTTP_CODE { 
        NO_REQUEST, // 请求不完整，继续读取客户请求
        GET_REQUEST, // 获得了一个完整的客户HTTP请求
        BAD_REQUEST, // 客户请求有语法错误 
        NO_RESOURCE, // 
        FORBIDDEN_REQUEST, 
        FILE_REQUEST, 
        INTERNAL_ERROR, // 服务器内部错误
        CLOSED_CONNECTION 
    };
    // 从状态机状态
    enum LINE_STATUS { 
        LINE_OK = 0, // 完整读取一行 
        LINE_BAD, // 报文语法有错误 
        LINE_OPEN // 读取的行不完整
    };

public:
    Http_conn () {}
    ~Http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn(bool real = true); 
    // 往读缓冲区读入数据
    bool read();
    /* 处理读入的数据，子线程通过调用process函数对任务进行处理，
       process 函数调用process_read函数和process_write函数分别完成报文解析与报文响应两个任务。
    */
    void process();

private:
    void init();
    // 完成报文解析
    HTTP_CODE process_read();
    // 完成报文响应
    bool process_write(HTTP_CODE ret);
    // 从状态机，负责读取报文的一行，返回值为读取状态
    LINE_STATUS parse_line();
    // 获取请求的一行
    char *get_line() {
        return m_read_buf + m_start_line;
    }
    /*
        主状态机部分
    */
    // 解析HTTP请求行，获得请求方法，目标url，http版本号，返回HTTP请求的状态
    HTTP_CODE parse_request_line(char *text);

public:
    static int m_epollfd;
    static int m_user_count;

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区中的数据大小
    int m_read_idx;
    // 指向读缓冲区中，将要解析的字符
    int m_checked_idx;
    // 读缓存区中，一行的起始位置
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    CHECK_STATE m_check_state;

    // 请求行中的数据
    METHOD m_method; // 请求方法
    char *m_url; // 请求资源
    char *m_version; // HTTP 版本
    int cgi; // 是否启用 POST

    // Http 请求是否要保持连接
    bool m_linger;


};


#endif