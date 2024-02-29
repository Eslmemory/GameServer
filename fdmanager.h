#pragma once
#include <memory>
#include <vector>
#include "mutex.h"
#include "singleton.h"

namespace WebServer {

	class FdContext : public std::enable_shared_from_this<FdContext> {
	public:
		typedef std::shared_ptr<FdContext> fdContextptr;
		FdContext(int fd);
		~FdContext();

		bool isInit() const { return m_IsInit; }
		bool isSocked() const { return m_IsSocket; }
		bool isClose() const { return m_IsClose; }
		
		void setUserNonblock(bool isBlock) { m_UserNonblock = isBlock; }
		bool getUserNonblock() const { return m_UserNonblock; }

		void setSysNonblock(bool isBlock) { m_SysNonblock = isBlock; }
		bool getSysNonblock() { return m_SysNonblock; }
		
		// type: ¶Á³¬Ê±/Ð´³¬Ê±
		void setTimeout(int type, uint64_t time);
		uint64_t getTimeout(int type);
	private:
		bool init();
	private:
		bool m_IsInit;
		bool m_IsSocket;
		bool m_IsClose;
		bool m_UserNonblock;
		bool m_SysNonblock;

		int m_Fd;

		uint64_t m_ReceiveTimeout;
		uint64_t m_SendTimeout;
	};

	class FdManager {
	public:
		typedef RWMutex RWMutexType;
		FdManager();
		FdContext::fdContextptr get(int fd, bool autoCreate = false);

		void del(int fd);
	private:
		RWMutexType m_Mtx;
		std::vector<FdContext::fdContextptr> m_Datas;
	};

	typedef Singleton<FdManager> FdMgr;
}