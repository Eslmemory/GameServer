#include "fdmanager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace WebServer {
	
	FdManager::FdManager() {
		m_Datas.resize(64);
	}

	FdContext::FdContext(int fd)
		:m_IsInit(false), m_IsSocket(false), m_SysNonblock(false), m_UserNonblock(false),
		 m_IsClose(false), m_Fd(fd), m_ReceiveTimeout(-1), m_SendTimeout(-1)
	{
		init();
	}

	FdContext::~FdContext() {

	}

	bool FdContext::init() {
		if (m_IsInit)
			return true;

		m_ReceiveTimeout = -1;
		m_SendTimeout = -1;
		
		struct stat fdState;
		if (fstat(m_Fd, &fdState)) {
			m_IsInit = false;
			m_IsSocket = false;
		}
		else {
			m_IsInit = true;
			m_IsSocket = S_ISSOCK(fdState.st_mode);
		}

		if (m_IsSocket) {
			int flags = fcntl_f(m_Fd, F_GETFL, 0);
			if (!(flags & O_NONBLOCK))
				fcntl_f(m_Fd, F_SETFL, flags | O_NONBLOCK);
			m_SysNonblock = true;
		}
		else {
			m_SysNonblock = false;
		}

		m_UserNonblock = false;
		m_IsClose = false;
		return m_IsInit;
	}

	void FdContext::setTimeout(int type, uint64_t time) {
		if (type == SO_RCVTIMEO)
			m_ReceiveTimeout = time;
		else
			m_SendTimeout = time;
	}

	uint64_t FdContext::getTimeout(int type) {
		if (type == SO_RCVTIMEO)
			return m_ReceiveTimeout;
		else
			return m_SendTimeout;
	}

	FdContext::fdContextptr FdManager::get(int fd, bool autoCreate) {
		if (fd == -1)
			return nullptr;
		RWMutexType::ReadLock lock(m_Mtx);
		if ((int)m_Datas.size() <= fd) {
			if (!autoCreate)
				return nullptr;
		}
		else {
			if (m_Datas[fd] || !autoCreate)
				return m_Datas[fd];
		}

		lock.unlock();

		RWMutexType::WriteLock lock2(m_Mtx);
		FdContext::fdContextptr context(new FdContext(fd));
		if (fd >= (int)m_Datas.size())
			m_Datas.resize(fd * 1.5);
		m_Datas[fd] = context;
		return context;
	}

	void FdManager::del(int fd) {
		RWMutexType::WriteLock lock(m_Mtx);
		if ((int)m_Datas.size() <= fd)
			return;
		m_Datas[fd].reset();
	}

}