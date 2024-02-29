#pragma once
#include <pthread.h>
#include <functional>

#include "mutex.h"

namespace WebServer {

	class Thread {
	public:
		typedef std::shared_ptr<Thread> threadPtr;

		Thread(std::function<void()> func, const std::string& name);
		~Thread();

		pid_t getId() const { return m_Id; }
		const std::string& getName() const;

		void join();
		static Thread* getThis();

		static void setName(const std::string& name);

	private:
		static void* run(void* args);

	private:
		pid_t m_Id = -1;
		pthread_t m_Thread = 0;
		std::function<void()> m_Func;
		std::string m_Name;
		Semaphore m_Semaphore;
	};
}