#include "iomanager.h"
#include "core.h"
#include "utils.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>

namespace WebServer {

	IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
		switch (event) {
		case IOManager::READ:
			return read;
		case IOManager::WRITE:
			return write;
		default:
			WS_ASSERT_WITHPARAM(false, "getContext");
		}
		throw std::invalid_argument("getContext invalid event");
	}

	void IOManager::FdContext::resetContext(EventContext& context) {
		context.scheduler = nullptr;
		context.fiber.reset();
		context.func = nullptr;
	}

	void IOManager::FdContext::triggerEvent(IOManager::Event event) {
		WS_ASSERT(event & events);
		events = (Event)(events & ~event);
		EventContext& context = getContext(event);
		if (context.func)
			context.scheduler->schedule(&context.func);
		else
			context.scheduler->schedule(&context.fiber);
		context.scheduler = nullptr;
		return;
	}

	IOManager::IOManager(size_t threads, bool useCaller, const std::string& name)
		: Scheduler(threads, useCaller, name)
	{
		m_EpollFd = epoll_create(5000);
		WS_ASSERT(m_EpollFd > 0);

		int rt = pipe(m_TickleFds);
		WS_ASSERT(!rt);

		epoll_event event;
		memset(&event, 0, sizeof(epoll_event));
		event.events = EPOLLIN | EPOLLET;
		event.data.fd = m_TickleFds[0];

		rt = fcntl(m_TickleFds[0], F_SETFL, O_NONBLOCK);
		WS_ASSERT(!rt);

		rt = epoll_ctl(m_EpollFd, EPOLL_CTL_ADD, m_TickleFds[0], &event);
		WS_ASSERT(!rt);

		contextResize(32);
		start();
	}

	IOManager::~IOManager() {
		stop();  // from scheduler
	}

	void IOManager::contextResize(size_t size) {
		m_FdContexts.resize(size);
		for (size_t i = 0; i < m_FdContexts.size(); i++) {
			if (!m_FdContexts[i]) {
				m_FdContexts[i] = new FdContext;
				m_FdContexts[i]->fd = i;
			}
		}
	}

	int IOManager::addEvent(int fd, Event event, std::function<void()> func) {
		FdContext* fdcontext = nullptr;
		RWMutexType::ReadLock lock(m_Mtx);
		if ((int)m_FdContexts.size() > fd) {
			fdcontext = m_FdContexts[fd];
			lock.unlock();
		}
		else {
			lock.unlock();
			RWMutexType::WriteLock lock2(m_Mtx);
			contextResize(fd * 1.5f);
			fdcontext = m_FdContexts[fd];
		}

		FdContext::MutexType::Lock lock2(fdcontext->mtx);
		if (WS_UNLIKELY(fdcontext->events & event)) {
			std::cout << "addEvent assert fd=" << fd
				<< " event=" << (EPOLL_EVENTS)event
				<< " fd_ctx.event=" << (EPOLL_EVENTS)fdcontext->events;
			WS_ASSERT(!(fdcontext->events & event));
		}

		int op = fdcontext->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
		epoll_event epollEvent;
		epollEvent.events = EPOLLET | fdcontext->events | event;
		epollEvent.data.ptr = fdcontext;

		int rt = epoll_ctl(m_EpollFd, op, fd, &epollEvent);
		if (rt) {
			WS_ASSERT(false);
			return -1;
		}

		++m_WaitingEventCount;
		fdcontext->events = (Event)(fdcontext->events | event);
		FdContext::EventContext& eventContext = fdcontext->getContext(event);
		WS_ASSERT(!eventContext.scheduler && !eventContext.fiber && !eventContext.func);

		eventContext.scheduler = Scheduler::getThis();
		if (func)
			eventContext.func.swap(func);
		else {
			eventContext.fiber = Fiber::getThis();
			WS_ASSERT((eventContext.fiber->getState() == Fiber::EXEC));
		}

		return 0;
	}

	bool IOManager::delEvent(int fd, Event event) {
		RWMutexType::ReadLock lock(m_Mtx);
		if ((int)m_FdContexts.size() <= fd)
			return false;
		FdContext* fdcontext = m_FdContexts[fd];
		lock.unlock();

		FdContext::MutexType::Lock lock2(fdcontext->mtx);
		if (WS_UNLIKELY(!(fdcontext->events & event)))
			return false;

		Event newEvents = (Event)(fdcontext->events & ~event);
		int op = newEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
		epoll_event epollEvent;
		epollEvent.events = EPOLLET | newEvents;
		epollEvent.data.ptr = fdcontext;

		int rt = epoll_ctl(m_EpollFd, op, fd, &epollEvent);
		if (rt) {
			printf("epoll_ctl failed in delEvent\n");
			return false;
		}

		--m_WaitingEventCount;
		fdcontext->events = newEvents;
		FdContext::EventContext& eventcontext = fdcontext->getContext(event);
		fdcontext->resetContext(eventcontext);
		return true;
	}

	bool IOManager::cancelEvent(int fd, Event event)
	{
		RWMutexType::ReadLock lock(m_Mtx);
		if (fd >= (int)m_FdContexts.size())
			return false;
		FdContext* fdcontext = m_FdContexts[fd];
		lock.unlock();

		FdContext::MutexType::Lock lock2(fdcontext->mtx);
		if (WS_UNLIKELY(!(fdcontext->events & event)))
			return false;
		Event newEvents = (Event)(fdcontext->events & ~event);
		int op = newEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
		epoll_event cancelEvent;
		cancelEvent.events = EPOLLET | newEvents;
		cancelEvent.data.ptr = fdcontext;

		int rt = epoll_ctl(m_EpollFd, op, fd, &cancelEvent);
		if (rt) {
			printf("epoll_ctl failed in cancelEvent\n");
			return false;
		}

		fdcontext->triggerEvent(event);
		--m_WaitingEventCount;
		return true;
	}

	bool IOManager::cancelAll(int fd) {
		RWMutexType::ReadLock lock(m_Mtx);
		if ((int)m_FdContexts.size() <= fd)
			return false;

		FdContext* fdcontext = m_FdContexts[fd];
		lock.unlock();

		FdContext::MutexType::Lock lock2(fdcontext->mtx);
		if (!fdcontext->events)
			return false;

		int op = EPOLL_CTL_DEL;
		epoll_event epevent;
		epevent.events = 0;
		epevent.data.ptr = fdcontext;

		int rt = epoll_ctl(m_EpollFd, op, fd, &epevent);
		if (rt) {
			std::cout << "epoll_ctl(" << m_EpollFd << ", "
				<< fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
				<< rt << " (" << errno << ") (" << strerror(errno) << ")";
			return false;
		}

		if (fdcontext->events & READ) {
			fdcontext->triggerEvent(READ);
			--m_WaitingEventCount;
		}
		if (fdcontext->events & WRITE) {
			fdcontext->triggerEvent(WRITE);
			--m_WaitingEventCount;
		}

		WS_ASSERT(fdcontext->events == 0);
		return true;
	}

	IOManager* IOManager::getThis() {
		return dynamic_cast<IOManager*>(Scheduler::getThis());
	}

	void IOManager::tickle() {
		if (!hasIdleThreads())
			return;
		int rt = write(m_TickleFds[1], "T", 1);
		WS_ASSERT(rt == 1);
	}

	void IOManager::idle() {
		printf("idle\n");
		const uint64_t MAX_EVENTS = 256;
		epoll_event* events = new epoll_event[MAX_EVENTS]();
		std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr) {
			delete[] ptr;
		});

		while (true) {
			uint64_t nextTimeout = 0;
			if (stopping(nextTimeout)) {
				printf("idle stopping exit\n");
				break;
			}

			int rt = 0;
			do {
				static const int MAX_TIMEOUT = 3000;
				if (nextTimeout != ~0ull)
					nextTimeout = (int)nextTimeout > MAX_TIMEOUT ? MAX_TIMEOUT : nextTimeout;
				else
					nextTimeout = MAX_TIMEOUT;
				rt = epoll_wait(m_EpollFd, events, MAX_EVENTS, (int)nextTimeout); // 等待直到有事件或者超时
				if (rt < 0 && errno == EINTR) {
				}
				else {
					break;
				}
			} while (true);

			std::vector<std::function<void()>> funcs;
			listExpiredFunc(funcs);
			if (!funcs.empty()) {
				schedule(funcs.begin(), funcs.end());
				funcs.clear();
			}
			
			for (int i = 0; i < rt; i++) {
				epoll_event& event = events[i];
				if (event.data.fd == m_TickleFds[0]) {
					uint8_t dummy[256];
					while (read(m_TickleFds[0], dummy, sizeof(dummy)) > 0);
					continue;
				}

				FdContext* fdcontext = (FdContext*)event.data.ptr;
				FdContext::MutexType::Lock lock(fdcontext->mtx);
				if (event.events & (EPOLLERR | EPOLLHUP))
					event.events |= (EPOLLIN | EPOLLOUT) & fdcontext->events;
				int realEvents = NONE;
				if (event.events & EPOLLIN)
					realEvents |= READ;
				if (event.events & EPOLLOUT)
					realEvents |= WRITE;

				if ((fdcontext->events & realEvents) == NONE)
					continue;

				int leftEvents = (fdcontext->events & ~realEvents);
				int op = leftEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
				event.events = EPOLLET | leftEvents;

				int rt2 = epoll_ctl(m_EpollFd, op, fdcontext->fd, &event);
				if (rt2) {
					printf("epoll falied in idle\n");
					continue;
				}

				if (realEvents & READ) {
					fdcontext->triggerEvent(READ);
					--m_WaitingEventCount;
				}

				if (realEvents & WRITE) {
					fdcontext->triggerEvent(WRITE);
					--m_WaitingEventCount;
				}
			}

			Fiber::fiberPtr cur = Fiber::getThis();
			auto rawPtr = cur.get();
			cur.reset();
			rawPtr->swapOut();
		}
	}

	bool IOManager::stopping(uint64_t& timeout) {
		return false;
	}

	bool IOManager::stopping() {
		// uint64_t timeout = 0;
		// return stopping(timeout);
		return false;
	}

	void IOManager::onTimerInsertedAtFront() {
		tickle();
	}

}