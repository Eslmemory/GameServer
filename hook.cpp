#include "hook.h"
#include <dlfcn.h>
#include "fdmanager.h"
#include "iomanager.h"

namespace WebServer {

	static bool thread_local t_HookEnable = false;

	#define HOOK_FUNC(FUNC) \
		FUNC(sleep) \
		FUNC(fcntl) \
		FUNC(socket) \
		FUNC(connect) \
		FUNC(send) \
		FUNC(recv) \
		FUNC(accept) \
		FUNC(close)

	void hookInit() {
		static bool isInited = false;
		if (isInited) {
			return;
		}
	// dlsym(RTLD_NEXT, "name")是在运行阶段进行查找,因此后面要先用nullptr对这些函数(sleep_f, fcntl_f等)进行初始化(定义),避免链接阶段找不到
	#define FUNC(name) name ## _f = (name ## _func)dlsym(RTLD_NEXT, #name);
			HOOK_FUNC(FUNC);
	#undef FUNC
	}

	static uint64_t s_ConnectTimeout = -1;
	struct _HookIniter {
		_HookIniter() {
			hookInit();
			// TODO: 加入配置文件
			s_ConnectTimeout = 1000;
		}
	};
	static _HookIniter s_HookIniter;
	
	bool isHookEnable() {
		return t_HookEnable;
	}

	void setHookEnable(bool flag) {
		t_HookEnable = flag;
	}
}

struct timerInfo {
	int cancelled = 0;
};

template<typename OriginFunc, typename... Args>
static ssize_t doIO(int fd, OriginFunc func, const char* hookFuncName, uint32_t event, int socketType, Args&&... args) {
	if (!WebServer::t_HookEnable)
		return func(fd, std::forward<Args>(args)...);
	
	WebServer::FdContext::fdContextptr context = WebServer::FdMgr::GetInstance()->get(fd);
	if (!context)
		return func(fd, std::forward<Args>(args)...);

	if (context->isClose()) {
		errno = EBADF;
		return -1;
	}

	if (!context->isSocked() || context->getUserNonblock())
		return func(fd, std::forward<Args>(args)...);

	uint64_t timeout = context->getTimeout(socketType);
	std::shared_ptr<timerInfo> tInfo(new timerInfo);

retry:
	ssize_t n = func(fd, std::forward<Args>(args)...);
	while (n == -1 && errno == EINTR) {
		n = func(fd, std::forward<Args>(args)...);
	}

	if (n == -1 && errno == EAGAIN) {
		WebServer::IOManager* ioManager = WebServer::IOManager::getThis();
		WebServer::Timer::timerPtr timer;
		std::weak_ptr<timerInfo> wInfo(tInfo);

		// 加入超时定时器,如果在规定时间内这个阻塞没有结束,就将wInfo/tInfo的cancelled设置为ETIMEDOUT,之后就会退出了
		// 如果阻塞结束了,即退出了这次doIO函数,那么tInfo就会被释放,此时如果已经设置了超时定时器,wInfo.lock就为nullptr,因此会直接返回
		if (timeout != (uint64_t)-1) {
			timer = ioManager->addConditionTimer(timeout, [wInfo, fd, ioManager, event]() {
				auto t = wInfo.lock();
				if (!t || t->cancelled)
					return;
				t->cancelled = ETIMEDOUT;
				ioManager->cancelEvent(fd, (WebServer::IOManager::Event)(event));
			}, wInfo);
		}

		// 加入事件的时候,也不需要再把处理函数func加进去了,因为反正都要goto到retry那里去,func还被记录着的
		int rt = ioManager->addEvent(fd, (WebServer::IOManager::Event)(event));
		if (rt) {
			// 事件没有加入成功,暂时取消定时器
			if (timer)
				timer->cancel();
			return -1;
		}
		else {
			// 这里先将协程暂停然后退出,由于退出后该协程是READY状态,因此scheduler会再次调用schedule将其加入协程队列中
			WebServer::Fiber::YieldToHold();
			// 协程又开始执行,先把之前的定时器取消了
			if (timer)
				timer->cancel();
			// 如果已经超时了,就直接退出了
			if (tInfo->cancelled) {
				errno = tInfo->cancelled;
				return -1;
			}
			goto retry;
		}
	}
	return n;
}

extern "C" {

#define FUNC(name) name ## _func name ## _f = nullptr;
	HOOK_FUNC(FUNC);
#undef FUNC

	unsigned int sleep(unsigned int seconds) {
		if (!WebServer::t_HookEnable) {
			return sleep_f(seconds);
		}

		WebServer::Fiber::fiberPtr fiber = WebServer::Fiber::getThis();
		WebServer::IOManager* ioManager = WebServer::IOManager::getThis();
		// std::bind()绑定后的函数,不管有无形参都可以让void()类型接收
		ioManager->addTimer(seconds * 1000, std::bind((void(WebServer::Scheduler::*)(WebServer::Fiber::fiberPtr, int thread)) & WebServer::IOManager::schedule, ioManager, fiber, -1));
		WebServer::Fiber::YieldToHold();
		return 0;
	}

	int socket(int domain, int type, int protocol) {
		if (!WebServer::t_HookEnable)
			return socket_f(domain, type, protocol);
		int fd = socket_f(domain, type, protocol);
		if (fd == -1)
			return fd;
		WebServer::FdMgr::GetInstance()->get(fd, true);
		return fd;
	}

	int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeoutMs) {
		if (!WebServer::t_HookEnable)
			return connect_f(fd, addr, addrlen);
		WebServer::FdContext::fdContextptr context = WebServer::FdMgr::GetInstance()->get(fd);
		if (!context || context->isClose()) {
			errno = EBADF;
			return -1;
		}

		if (!context->isSocked())
			return connect_f(fd, addr, addrlen);

		if (context->getUserNonblock())
			return connect_f(fd, addr, addrlen);

		int n = connect_f(fd, addr, addrlen);
		if (n == 0)
			return 0;
		else if (n != -1 || errno != EINPROGRESS)
			return n;

		WebServer::IOManager* ioManager = WebServer::IOManager::getThis();
		WebServer::Timer::timerPtr timer;
		std::shared_ptr<timerInfo> tInfo(new timerInfo);
		std::weak_ptr<timerInfo> wInfo(tInfo);

		if (timeoutMs != (uint64_t)-1) {
			timer = ioManager->addConditionTimer(timeoutMs, [wInfo, fd, ioManager] {
				auto t = wInfo.lock();
				if (!t || t->cancelled)
					return;
				t->cancelled = ETIMEDOUT;
				ioManager->cancelEvent(fd, WebServer::IOManager::WRITE);
			}, wInfo);
		}

		int rt = ioManager->addEvent(fd, WebServer::IOManager::WRITE);
		if (rt == 0) {
			WebServer::Fiber::YieldToHold();
			if (timer) {
				timer->cancel();
			}
			if (tInfo->cancelled) {
				errno = tInfo->cancelled;
				return -1;
			}
		}
		else {
			if (timer) {
				timer->cancel();
			}
		}

		int error = 0;
		socklen_t len = sizeof(int);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
			return -1;
		if (!error)
			return 0;
		else {
			errno = error;
			return -1;
		}
	}

	int connect(int fd, const struct sockaddr* addr, socklen_t addrlen) {
		connect_with_timeout(fd, addr, addrlen, WebServer::s_ConnectTimeout);
	}

	int accept(int s, struct sockaddr* addr, socklen_t* addrlen) {
		int fd = doIO(s, accept_f, "accept", WebServer::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
		if (fd >= 0) {
			WebServer::FdMgr::GetInstance()->get(fd, true);
		}
		return fd;
	}

	ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
		return doIO(sockfd, recv_f, "recv", WebServer::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
	}

	ssize_t send(int s, const void* msg, size_t len, int flags) {
		return doIO(s, send_f, "send", WebServer::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
	}

	int close(int fd) {
		if (!WebServer::t_HookEnable) {
			return close_f(fd);
		}

		WebServer::FdContext::fdContextptr context = WebServer::FdMgr::GetInstance()->get(fd);
		if (context) {
			auto iom = WebServer::IOManager::getThis();
			if (iom) {
				iom->cancelAll(fd);
			}
			WebServer::FdMgr::GetInstance()->del(fd);
		}
		return close_f(fd);
	}

	// fcntl覆盖了系统实现的fcntl,这种同名函数的覆盖只有动态链接能够实现。因为动态链接时,会优先使用第一个找到的函数定义,之后就定义就不再加载了。
	// 而这里的fcntl是直接在项目包含的源文件中的,所以链接的时候是最优先找到的,之后就不会加载系统的fcntl实现了。
	// 而宏定义中dlsym(RTLD_NEXT, #name)是去共享库里面找,因此源文件不算是共享库;其次RTLD_NEXT是从dlsym(RTLD_NEXT, #name)所在库的下一个库开始找。
	int fcntl(int fd, int cmd, ...) {
		va_list va;
		va_start(va, cmd);
		switch (cmd) {
		case F_SETFL:
		{
			int arg = va_arg(va, int);
			va_end(va);
			WebServer::FdContext::fdContextptr context = WebServer::FdMgr::GetInstance()->get(fd);
			if (!context || context->isClose() || !context->isSocked())
				return fcntl_f(fd, cmd, arg);
			context->setUserNonblock(arg & O_NONBLOCK);
			if (context->getSysNonblock())
				arg |= O_NONBLOCK;
			else
				arg &= ~O_NONBLOCK;
			return fcntl_f(fd, cmd, arg);
		}
		break;
		case F_GETFL:
		{
			va_end(va);
			int arg = fcntl_f(fd, cmd);
			WebServer::FdContext::fdContextptr context = WebServer::FdMgr::GetInstance()->get(fd);
			if (!context || context->isClose() || !context->isSocked())
				return arg;
			if (context->getSysNonblock())
				return arg |= O_NONBLOCK;
			else
				return arg &= ~O_NONBLOCK;
		}
		break;
		// TODO: other fd
		default:
			va_end(va);
			return fcntl_f(fd, cmd);
		}
	}

}