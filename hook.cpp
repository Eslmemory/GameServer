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
	// dlsym(RTLD_NEXT, "name")�������н׶ν��в���,��˺���Ҫ����nullptr����Щ����(sleep_f, fcntl_f��)���г�ʼ��(����),�������ӽ׶��Ҳ���
	#define FUNC(name) name ## _f = (name ## _func)dlsym(RTLD_NEXT, #name);
			HOOK_FUNC(FUNC);
	#undef FUNC
	}

	static uint64_t s_ConnectTimeout = -1;
	struct _HookIniter {
		_HookIniter() {
			hookInit();
			// TODO: ���������ļ�
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

		// ���볬ʱ��ʱ��,����ڹ涨ʱ�����������û�н���,�ͽ�wInfo/tInfo��cancelled����ΪETIMEDOUT,֮��ͻ��˳���
		// �������������,���˳������doIO����,��ôtInfo�ͻᱻ�ͷ�,��ʱ����Ѿ������˳�ʱ��ʱ��,wInfo.lock��Ϊnullptr,��˻�ֱ�ӷ���
		if (timeout != (uint64_t)-1) {
			timer = ioManager->addConditionTimer(timeout, [wInfo, fd, ioManager, event]() {
				auto t = wInfo.lock();
				if (!t || t->cancelled)
					return;
				t->cancelled = ETIMEDOUT;
				ioManager->cancelEvent(fd, (WebServer::IOManager::Event)(event));
			}, wInfo);
		}

		// �����¼���ʱ��,Ҳ����Ҫ�ٰѴ�����func�ӽ�ȥ��,��Ϊ������Ҫgoto��retry����ȥ,func������¼�ŵ�
		int rt = ioManager->addEvent(fd, (WebServer::IOManager::Event)(event));
		if (rt) {
			// �¼�û�м���ɹ�,��ʱȡ����ʱ��
			if (timer)
				timer->cancel();
			return -1;
		}
		else {
			// �����Ƚ�Э����ͣȻ���˳�,�����˳����Э����READY״̬,���scheduler���ٴε���schedule�������Э�̶�����
			WebServer::Fiber::YieldToHold();
			// Э���ֿ�ʼִ��,�Ȱ�֮ǰ�Ķ�ʱ��ȡ����
			if (timer)
				timer->cancel();
			// ����Ѿ���ʱ��,��ֱ���˳���
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
		// std::bind()�󶨺�ĺ���,���������βζ�������void()���ͽ���
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

	// fcntl������ϵͳʵ�ֵ�fcntl,����ͬ�������ĸ���ֻ�ж�̬�����ܹ�ʵ�֡���Ϊ��̬����ʱ,������ʹ�õ�һ���ҵ��ĺ�������,֮��Ͷ���Ͳ��ټ����ˡ�
	// �������fcntl��ֱ������Ŀ������Դ�ļ��е�,�������ӵ�ʱ�����������ҵ���,֮��Ͳ������ϵͳ��fcntlʵ���ˡ�
	// ���궨����dlsym(RTLD_NEXT, #name)��ȥ�����������,���Դ�ļ������ǹ����;���RTLD_NEXT�Ǵ�dlsym(RTLD_NEXT, #name)���ڿ����һ���⿪ʼ�ҡ�
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