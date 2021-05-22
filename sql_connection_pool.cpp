#include <cstdio>
#include <cstdlib>
#include "sql_connection_pool.h"

// 初始化连接池
void Connection_pool::init(std::string url, std::string user, std::string password, std::string database_name, int port, 
          unsigned int max_conn)  {
    // 初始化数据库信息
    this->url = url;
    this->port = port;
    this->user = user;
    this->password = password;
    this->database_name = database_name;

    // 创建 max_conn 条数据库连接
    m_mutex.lock();
    for (int i = 0; i < max_conn; ++i) {
        MYSQL *conn = nullptr;
        conn = mysql_init(conn);

        if (conn == nullptr) {
            fprintf(stderr, "Error: %s in [%s-%d-%s]\n", mysql_error(conn), __FILE__, __LINE__, __FUNCTION__);
            exit(1);
        } 
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), database_name.c_str(), port, NULL, 0);
        if (conn == nullptr) {
            fprintf(stderr, "Error: %s in [%s-%d-%s]\n", mysql_error(conn), __FILE__, __LINE__, __FUNCTION__);
            exit(1);
        }
        // 数据库连接加入连接池
        conn_list.push_back(conn);
        // 更新空闲连接数量
        ++free_conn;
    }
    // 将信号量初始化为最大连接次数
    reserve = Sem(free_conn);

    this->max_conn = free_conn;
    m_mutex.unlock();
}

// 当有请求时，获取连接池中的空闲连接 
MYSQL *Connection_pool::get_connection() {
    MYSQL *conn = nullptr;

    if (0 == conn_list.size()) {
        return nullptr;
    }

    // 取出连接，信号量减一，信号量为0则等待
    reserve.wait();
    m_mutex.lock();
    conn = conn_list.front();
    conn_list.pop_front();
    --free_conn;
    ++cur_conn;
    m_mutex.unlock();
    return conn;
}


// 释放当前使用的连接
bool Connection_pool::release_connection(MYSQL *conn) {
    if (nullptr == conn) {
        return false;
    }
    m_mutex.lock();
    conn_list.push_back(conn);
    ++free_conn;
    --cur_conn;
    m_mutex.unlock();
    reserve.post();

    return true;
}


//销毁数据库连接池
void Connection_pool::destroy_pool()
{

	m_mutex.lock();
	if (conn_list.size() > 0)
	{
		std::list<MYSQL *>::iterator it;
		for (it = conn_list.begin(); it != conn_list.end(); ++it)
		{
			MYSQL *conn = *it;
			mysql_close(conn);
		}
		cur_conn = 0;
		free_conn = 0;
		conn_list.clear();

	}

	m_mutex.unlock();
}

//当前空闲的连接数
int Connection_pool::get_free_conn()
{
	return this->free_conn;
}


// 通过RAII机制获取与释放数据库连接
ConnectionRAII::ConnectionRAII(MYSQL **SQL, Connection_pool *conn_pool){
	*SQL = conn_pool->get_connection();
	
	conn_RAII = *SQL;
	pool_RAII = conn_pool;
}

ConnectionRAII::~ConnectionRAII(){
	pool_RAII->release_connection(conn_RAII);
}