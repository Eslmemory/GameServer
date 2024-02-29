#include "mutex.h"

namespace WebServer {

	Semaphore::Semaphore(uint32_t count) {
		if (sem_init(&m_Semaphore, 0, count)) {
			throw std::logic_error("sem_init error");
		}
	}

	Semaphore::~Semaphore() {
		sem_destroy(&m_Semaphore);
	}
	
	// sem_wait成功返回0,失败返回-1
	void Semaphore::wait() {
		if (sem_wait(&m_Semaphore)) {
			throw std::logic_error("sem_wait error");
		}
	}
	
	void Semaphore::notify() {
		if (sem_post(&m_Semaphore)) {
			throw std::logic_error("sem_post error");
		}
	}
}