#ifndef __WRAP_H_
#define __WRAP_H_

void perr_exit(const char *msg);
int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen); 
int Listen(int sockfd, int backlog);
int Socket(int domain, int type, int protocol);
ssize_t Read(int fd, void *buf, size_t count);
ssize_t Write(int fd, const void *buf, size_t count);
int Close(int fd);
ssize_t Readn(int fd, void *vptr, size_t n);
ssize_t Writen(int fd, const void *vptr, size_t n);
ssize_t my_read(int fd, char *ptr);
ssize_t Readline(int fd, void *vptr, size_t maxlen);
pid_t Fork(void);
#endif
