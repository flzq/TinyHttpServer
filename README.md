# TinyHttpServer

# Linux下的C++微型HTTP服务器

## 使用技术

Linux、C/C++、Socket编程、TCP、MySQL、单例模式、LRU、线程池

## 项目介绍

1. 该项目为 Linux 下的 C++ 微型 HTTP 服务器；

2. 基于模拟的Proactor事件处理模式

3. 基于半同步/半反应堆（半异步）模式

&ensp;&ensp;&ensp;&ensp;1. 使用线程池处理客户端与服务器通信

&ensp;&ensp;&ensp;&ensp;2. 基于epoll的ET模式

4. 基于SIGALARM信号通知机制实现

5. 统一事件源：将I/O时间和信号事件通过epoll进行监听

6. 实现基于LRU的定时器处理非活跃连接

&ensp;&ensp;&ensp;&ensp;1. 基于信号机制进行管理

7. 基于单例模式实现日志系统和数据库连接池

8. 通过数据库进行用户的登录和注册校验

9. 利用线程池提供并发服务管理HTTP连接

## 快速运行

- 服务器环境

&ensp;&ensp;&ensp;&ensp;- 腾讯云轻量应用服务器

&ensp;&ensp;&ensp;&ensp;- Debian 10.2 64bit

&ensp;&ensp;&ensp;&ensp;- MySQL 5.6

- 修改server.cpp中数据库初始化信息

```C++
// 创建数据库连接池
Connection_pool *conn_pool = Connection_pool::get_instance();
conn_pool->init(连接ip, 用户名, 密码, 数据库名, 端口, 最大连接数);

```


- 获取代码

```C++
git clone https://github.com/flzq/TinyHttpServer.git
```


- 编译

```C++
cd TinyHttpServer
make
make clean // 清除中间文件
```


- 启动

```C++
// 服务端在9999端口监听，可以在server.cpp中修改
./server
```


- 测试

```C++
ip:port
```


## 模块和解决方案

1. 该项目为 Linux 下的 C++ 轻量级 Web 服务器；

2. 基于模拟的Proactor事件处理模式

3. 基于半同步/半反应堆（半异步）模式

&ensp;&ensp;&ensp;&ensp;1. 工作线程基于同步模式

&ensp;&ensp;&ensp;&ensp;2. 主线程基于异步模式（采用信号通知机制）

&ensp;&ensp;&ensp;&ensp;3. 主线程采用模拟的Proactor事件处理模式

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 主线程将数据等信息封装为一个Http对象，插入请求队列中

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 工作线程从请求队列中取得

&ensp;&ensp;&ensp;&ensp;4. 该种方式的缺陷

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 主线程和工作线程共享请求队列，请求队列的访问为互斥的，需要加锁，耗费CPU资源；

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 每个工作线程同一时间只能处理一个客户请求。如果客户数量较多，而工作线程较少，则请求队列中将堆积很多任务对象，对客户端的响应变慢。如果增加工作线程，则工作线程的切换也需要耗费CPU资源；

&ensp;&ensp;&ensp;&ensp;5. 解决方法

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 主线程只需要对监听socket进行管理，与客户端建立新的连接后，连接socket由工作线程管理;

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 主线程向工作线程派发连接socket；

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;3. 每个线程都工作在异步模式下；

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;4. 每个工作线程都可以管理多个客户连接；

4. 主线程工作流程

5. 工作线程工作流程（对客户端传来HTTP请求进行处理）：process函数

&ensp;&ensp;&ensp;&ensp;1. 解析HTTP请求报文（process_read函数）

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 主从状态机解析报文

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 解析完报文后，通过do_request函数对请求的资源进行分析

&ensp;&ensp;&ensp;&ensp;2. 生成HTTP响应报文（process_write函数）

6. 基于SIGALRM信号和统一事件源通知主循环，执行相应的定时事件处理代码

&ensp;&ensp;&ensp;&ensp;1. 统一事件源：在主线程的主循环中统一处理信号和I/O事件

&ensp;&ensp;&ensp;&ensp;2. 利用alarm函数定期地触发SIGALRM信号，执行相应的信号处理函数（server.cpp 中的 sig_handler函数）

&ensp;&ensp;&ensp;&ensp;3. 在信号处理函数中利用创建的**管道写端** 通知主线程中的主循环（统一事件源），主循环通过I/O复用机制监听**管道的读端** ，从而在主循环中处理相应的信号；

&ensp;&ensp;&ensp;&ensp;4. 主循环根据收到的信号值执行该信号对应的逻辑代码（如timer_handler函数处理非活跃链接）

7. 定时器处理非活跃连接

&ensp;&ensp;&ensp;&ensp;1. 基于LRU（最近最少使用）的定时器

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 为每一个链接创建一个定时器

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 使用升序链表将所有定时器连接起来

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;3. 使用哈希数组存储客户端socket描述符与定时器指针的映射

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;4. 定时器中注册了对于关闭非活跃连接的回调函数

&ensp;&ensp;&ensp;&ensp;2. 基于时间堆的定时器

8. 日志系统

&ensp;&ensp;&ensp;&ensp;1. 基于单例模式实现日志系统

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 基于静态局部变量懒汉模式（C++11之后不需要加锁也是线程安全的）

&ensp;&ensp;&ensp;&ensp;2. 有同步和异步两种日志写入方式

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 同步方式：写入函数与工作线程串行执行

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 异步方式：通过生产者-消费者模型写日志（通过阻塞队列）

&ensp;&ensp;&ensp;&ensp;3. 阻塞队列中实现了生产者-消费者模型，用于异步模式中日志信息的读写

&ensp;&ensp;&ensp;&ensp;4. 日志类中的方法都不会被调用，都是通过定义的可变参数宏调用

```C++
/*
  位于 log.h 文件末尾
*/
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)

```


9. 数据库连接池：用于注册和登录校验

&ensp;&ensp;&ensp;&ensp;1. 基于单例模式实现数据库连接池

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 基于静态局部变量懒汉模式（C++11之后不需要加锁也是线程安全的）

&ensp;&ensp;&ensp;&ensp;2. 数据库访问流程

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 创建数据库连接

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 完成数据库操作

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;3. 断开数据库连接

&ensp;&ensp;&ensp;&ensp;3. 为什么使用数据库连接池

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 如果程序需要频繁访问数据库，则需要不断创建和断开数据库连接，这是一个耗时操作

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. 程序开始运行时，集中创建多个数据库连接，将数据库连接进行集中管理，可以保证程序运行速度，也可使得操作更加安全

&ensp;&ensp;&ensp;&ensp;4. 获取和释放一个数据库连接：通过RAII机制

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;1. 使用RAII机制管理连接的获取和释放

&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2. RAII将获得和释放一个连接与类对象的生命周期绑定

10. 注册和登录功能

&ensp;&ensp;&ensp;&ensp;1. 使用数据库连接池实现服务器访问数据库的功能

&ensp;&ensp;&ensp;&ensp;2. 基于POST请求完成注册和登录校验

