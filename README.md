# Linux下的轻量级HTTP服务器

## 使用技术

Linux、C++、Socket 编程、TCP、MySQL


## 项目介绍：

1. 该项目为 Linux 下的 C++ 轻量级 Web 服务器；
2. 使用非阻塞socket+epoll（ET模式），基于 Proactor 事件处理模式；
3. 使用线程池管理 HTTP 链接；
4. 使用状态机解析 HTTP 请求报文，实现 GET/POST 两种请求解析；
5. 基于单例模式实现了日志系统；
6. 基于Linux信号机制+升序链表实现定时器，用于处理非活动链接；## 