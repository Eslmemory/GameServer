#pragma once
#include <functional>
#include <pthread.h>
#include <semaphore.h>
#include <memory>

namespace WebServer {
	
	// Semaphore
	class Semaphore {
	public:

		Semaphore(uint32_t count = 0);
		~Semaphore();

		void wait();
		void notify();
	private:
		sem_t m_Semaphore;
	};

	// Lock
	template<typename T>
	class LockImpl {
	public:
		LockImpl(T& mutex)
			: m_Mutex(mutex)
		{
			lock();
		}

		~LockImpl() {
			unlock();
		}

		void lock() {
			if (!m_Locked) {
				m_Mutex.lock();
				m_Locked = true;
			}
		}

		void unlock() {
			if (m_Locked) {
				m_Mutex.unlock();
				m_Locked = false;
			}
		}

	private:
		bool m_Locked = false;
		T& m_Mutex;
	};

	// ReadLock
	template<typename T>
	class ReadLockImpl {
	public:
		ReadLockImpl(T& mutex)
			: m_Mutex(mutex)
		{
			rdlock();
		}

		~ReadLockImpl() {
			unlock();
		}

		void rdlock() {
			if (!m_Locked) {
				m_Mutex.rdlock();
				m_Locked = true;
			}
		}

		void unlock() {
			if (m_Locked) {
				m_Mutex.unlock();
				m_Locked = false;
			}
		}

	private:
		bool m_Locked = false;
		T& m_Mutex;
	};

	// WriteLock
	template<typename T>
	class WriteLockImpl {
	public:
		WriteLockImpl(T& mutex)
			: m_Mutex(mutex)
		{
			wrlock();
		}

		~WriteLockImpl() {
			unlock();
		}

		void wrlock() {
			if (!m_Locked) {
				m_Mutex.wrlock();
				m_Locked = true;
			}
		}

		void unlock() {
			if (m_Locked) {
				m_Mutex.unlock();
				m_Locked = false;
			}
		}

	private:
		bool m_Locked = false;
		T& m_Mutex;
	};

	// Mutex
	class Mutex {
	public:
		typedef LockImpl<Mutex> Lock;

		Mutex(const Mutex&) = delete;
		Mutex& operator=(const Mutex&) = delete;

		Mutex() {
			pthread_mutex_init(&m_Mutex, nullptr);
		}

		~Mutex() {
			pthread_mutex_destroy(&m_Mutex);
		}

		void lock() {
			pthread_mutex_lock(&m_Mutex);
		}

		void unlock() {
			pthread_mutex_unlock(&m_Mutex);
		}

	private:
		pthread_mutex_t m_Mutex;
	};

	// RWMutex
	class RWMutex {
	public:
		typedef ReadLockImpl<RWMutex> ReadLock;
		typedef WriteLockImpl<RWMutex> WriteLock;

		RWMutex(const RWMutex&) = delete;
		RWMutex& operator=(const RWMutex&) = delete;

		RWMutex() {
			pthread_rwlock_init(&m_Lock, nullptr);
		}

		~RWMutex() {
			pthread_rwlock_destroy(&m_Lock);
		}

		void rdlock() {
			pthread_rwlock_rdlock(&m_Lock);
		}

		void wrlock() {
			pthread_rwlock_wrlock(&m_Lock);
		}

		void unlock() {
			pthread_rwlock_unlock(&m_Lock);
		}

	private:
		pthread_rwlock_t m_Lock;
	};
}