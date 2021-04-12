#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "wrap.h"

void perr_exit(const char *msg) {
	perror(msg);
	exit(-1);
}

int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	int n;
again:
	if ((n = accept(sockfd, addr, addrlen)) < 0) {
		if ((errno == EINTR) || (errno == ECONNABORTED)) {
			goto again;
		}	
		else {
			perr_exit("accept error");
		}
	}

	return n;
}

int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int n;
	n = bind(sockfd, addr, addrlen);
	if (n == -1) {
		perr_exit("bind error");
	}

	return n;
}

int Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int n;
	if ((n = connect(sockfd, addr, addrlen)) < 0)
			perr_exit("connect error");
	return n;	
}

int Listen(int sockfd, int backlog) {
	int n;
	if ((n = listen(sockfd, backlog)) < 0)
		perr_exit("listen error");

	return n;
}

int Socket(int domain, int type, int protocol) {
	int n;
	if ((n = socket(domain, type, protocol)) < 0)
		perr_exit("socket error");

	return n;
}


ssize_t Read(int fd, void *buf, size_t count) {
	int n;
again:
	if((n = read(fd, buf, count)) == -1) {
		if (errno == EINTR)
			goto again;
		else
			return -1;
	}
	return n;
	
}

ssize_t Write(int fd, const void *buf, size_t count) {
	int n;
again:
	if ((n = write(fd, buf, count)) == -1) {
		if (errno == EINTR)
			goto again;
		else
			return -1;
	}
	return n;
}

int Close(int fd) {
	int n;
	if ((n = close(fd)) == -1)
		perr_exit("close error");

	return n;
}

ssize_t Readn(int fd, void *vptr, size_t n) {
	size_t nleft = n;
	ssize_t nread;
	char *bufp = (char*)vptr;
	while (nleft > 0) {
		if ((nread = read(fd, bufp, nleft)) < 0) {
			if (errno == EINTR) {
				nread = 0;
			}
			else
				return -1;	
		}	
		else if (nread == 0) {
			break;	
		}
		nleft -= nread;
		bufp += nread;
	}

	return n - nleft;
}

ssize_t Writen(int fd, const void *vptr, size_t n) {
	size_t nleft = n;
	ssize_t nwrite = 0;
	char *bufp = (char*)vptr;
	while (nleft > 0) {
		if ((nwrite = write(fd, bufp, nleft)) < 0) {
			if (errno == EINTR)
				nwrite = 0;
			else
				return -1;	
		}
		nleft -= nwrite;
		bufp += nwrite;
	}

	return n - nleft;
}

pid_t Fork(void) {
	pid_t pid;
	if((pid = fork()) < 0)
		perr_exit("fork error");
	return pid;
}

ssize_t my_read(int fd, char *ptr);
ssize_t Readline(int fd, void *vptr, size_t maxlen);

