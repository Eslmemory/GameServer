#pragma once
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

namespace WebServer {

	bool isHookEnable();
	void setHookEnable(bool flag);

}


extern "C" {
	typedef unsigned int (*sleep_func)(unsigned int seconds);
	extern sleep_func sleep_f;

	typedef int (*fcntl_func)(int fd, int cmd, ...);
	extern fcntl_func fcntl_f;

	// socket
	typedef int(*socket_func)(int domain, int type, int protocol);
	extern socket_func socket_f;

	typedef int(*connect_func)(int fd, const struct sockaddr* addr, socklen_t addrlen);
	extern connect_func connect_f;

	typedef int (*accept_func)(int s, struct sockaddr* addr, socklen_t* addrlen);
	extern accept_func accept_f;

	typedef ssize_t(*send_func)(int fd, const void* msg, size_t n, int flags);
	extern send_func send_f;

	typedef ssize_t(*recv_func)(int sockfd, void* buf, size_t len, int flags);
	extern recv_func recv_f;

	typedef int (*close_func)(int fd);
	extern close_func close_f;

	extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeoutMs);
}