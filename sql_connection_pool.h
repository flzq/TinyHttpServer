/*
数据库连接池
    * 单例模式：静态局部变量懒汉模式创建
    * list实现连接池
    * 连接池为静态大小
    * 互斥锁实现线程安全        
*/


#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <string>
#include <list>
#include "/www/server/mysql/include/mysql.h"
#include "lock.h"

class Connection_pool {
public:
    // 静态局部变量获取单例模式
    static Connection_pool *get_instance() {
        static Connection_pool instance;
        return &instance;
    }

    // 初始化连接池
    void init(std::string url, std::string user, std::string password, std::string database_name, int port, 
              unsigned int max_conn);

    MYSQL *get_connection(); // 获取数据库连接
    bool release_connection(MYSQL *conn); // 释放连接
    int get_free_conn(); // 获取连接
    void destroy_pool(); // 销毁所有连接

    // RAII机制销毁连接池
    ~Connection_pool() {
        destroy_pool();
    };

private:
    Connection_pool() : cur_conn(0), free_conn(0) {}

private:
    unsigned int max_conn; // 最大连接数
    unsigned int cur_conn; // 当前已使用的连接数
    unsigned int free_conn; // 当前空闲的连接数

    Locker m_mutex; // 多线程获取连接时，对连接池进行互斥操作，保证线程安全
    Sem reserve; // 基于信号量实现多线程争夺连接的同步机制，表示连接池中空闲连接数量
    std::list<MYSQL *> conn_list; // 连接池

    std::string url; // 主机地址
    std::string port; // 数据库端口
    std::string user; // 数据库用户名
    std::string password; // 数据库密码
    std::string database_name; // 数据库名
};

// RAII 机制：将数据库的连接与释放通过RAII机制封装，避免手动释放
class ConnectionRAII {
public:
    ConnectionRAII(MYSQL **con, Connection_pool *conn_pool);
    ~ConnectionRAII();

private:
    MYSQL *conn_RAII;
    Connection_pool *pool_RAII;
};


#endif