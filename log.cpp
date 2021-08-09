#include <pthread.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "log.h"


Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if (m_fp != nullptr) {
        fclose(m_fp);
    }
}

bool Log::init(const char *filename, int log_buf_size, int split_lines, int max_queue_size) {
    /*
        初始化日志文件，实现日志创建
        日志名：事件_filename；（其中时间为启动时间，日志名如：2021_06_01_ServerLog）
    */

    // 如果设置了max_queue_size，则采用异步
    if (max_queue_size >= 1) {  // 异步需要设置阻塞队列的长度，同步不需要设置
        // 需要异步写日志
        m_is_async = true;
        // 创建阻塞队列
        m_log_queue = new Block_queue<std::string>(max_queue_size);

        // 创建线程，用于日志的异步写
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    // 日志内容长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf));

    // 日志的最大行数
    m_split_lines = split_lines;

    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 获得文件名
    const char *p = strrchr(filename, '/'); // 从后往前找到第一个 / 的位置
    char log_full_name[256] = {0};

    // 存储时日志文件名：时间_文件名
    if (p == nullptr) { // 文件名中不含有 /
        strcpy(log_name, filename); // 当日志系统已经运行后，修复当filename不包含"/"时，在write_log函数中，创建新的日志文件的文件名中只包含日期的错误
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year+1900, my_tm.tm_mon+1, my_tm.tm_mday, filename);
    }
    else {
        strcpy(log_name, p+1); // 获得日志文件名
        strncpy(dir_name, filename, p-filename+1); // 获得存放日志的路径名

        // 获得日志文件的绝对路径名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year+1900, my_tm.tm_mon+1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == nullptr) {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    /*
        日志级别：DEBUG, INFO, WARN（未使用）, ERROR
            DEBUG：调试代码时的输出，在系统实际运行时，一般不使用；
            Warn：这种警告与调试时终端的warning类似，同样是调试代码时使用；
            Info：报告系统当前的状态，当前执行的流程或接收的信息等；
            Error：输出系统的错误信息；
        
        日志文件按照最大行数、不同日期分文件：
            日志写入前判断当前时间是否为创建日志时间，行数是否超过最大行数限制
                若为创建日志时间，写入日志，否则创建当前时间的新的日志文件
                若当前文件已经到了最大行数，创建新的日志文件，文件名为在日志文件名末尾加上（m_count/m_split_lines）

        将要写入日志的信息进行格式化
    */


   // 获取当前时间
   struct timeval now = {0, 0};
   gettimeofday(&now, NULL);
   time_t t = now.tv_sec;
   struct tm *sys_tm = localtime(&t);
   struct tm my_tm = *sys_tm;
   char s[16] = {0};

    // 日志分级
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }


    m_mutex.lock();
    // 日志行数记录
    m_count++; // 更新行数

    // 日志不是今天或写入的日志行数是最大行的倍数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        // 新的日志文件
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        // 更新日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        // 如果时间不是今天，创建当天的日志文件，更新 m_today 和 m_count
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else // 时间是当天，但是日志行数是最大行的倍数，创建新的日志，日志名：之前日志名的基础上加后缀（m_count/m_split_lines）
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    // 可变参数初始化
    va_list valst;
    va_start(valst, format);

    std::string log_str;
    m_mutex.lock();

    //格式化写入日志的内容，写入的内容格式：时间+内容
    // 时间格式化
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 内容格式化
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 若 m_is_async 为 true表示异步，默认为同步
    if (m_is_async && !m_log_queue->full()) // 若异步，则将日志信息加入阻塞队列
    {
        m_log_queue->push(log_str);
    }
    else // 同步写日志，直接对日志文件加锁写
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp); // 若同步，则加锁向文件中写
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
