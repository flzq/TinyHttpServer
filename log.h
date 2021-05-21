/*
基于静态局部变量懒汉模式实现日志类，C++11之后可以不用锁
日志级别：DEBUG, INFO, WARN（未使用）, ERROR
DEBUG：调试代码时的输出，在系统实际运行时，一般不使用；
Warn：这种警告与调试时终端的warning类似，同样是调试代码时使用；
Info：报告系统当前的状态，当前执行的流程或接收的信息等；
Error：输出系统的错误信息；
*/

#ifndef LOG_H
#define LOG_H

#include <string>
#include "block_queue.h"
#include "lock.h"

class Log {
private:
    Log() {}
    virtual ~Log();

    // 异步写日志方法
    void *async_write_log() {
        std::string single_log;

        // 从阻塞队列中取出一条日志内容，写入文件
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

public:
    // C++11之后，静态局部变量懒汉模式获取实例不需要加锁
    static Log *get_instance() {
        static Log log;
        return &log;
    }

    // 异步写日志公有方法，调用私有方法 async_write_log
    static void *flush_log_thread(void *args) {
        Log::get_instance()->async_write_log();
    }

    /*
        filename: 日志文件名
        lgo_buf_size：日志缓冲区大小
        split_lines：最大行数
        max_queue_size：日志队列长队
    */
    bool init(const char *filename, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    // 将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);

    // 强制刷新缓冲区
    void flush(void);

private:
    char dir_name[128]; // 路径名
    char log_name[128]; // 日志文件名
    int m_split_lines; // 日志最大行数
    int m_log_buf_size; // 日志缓冲区大小
    long long m_count; // 日志行数记录
    int m_today; // 按天分文件，记录当前时间是哪一天
    Block_queue<std::string> *m_log_queue; // 阻塞队列
    Locker m_mutex; // 互斥锁：写日志文件时进行同步
    FILE *m_fp; // 日志文件指针
    char *m_buf; // 要输出的内容
    bool m_is_async; // 是否同步标志位

};

/* 
这四个宏在其他文件中使用，用于不同等级的日志的输出
日志类中的方法都不会被其他函数直接调用，使用这四个宏供其他程序调用
*/
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)

#endif