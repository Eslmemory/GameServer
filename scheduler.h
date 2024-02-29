#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include "mutex.h"
#include "fiber.h"
#include "thread.h"

namespace WebServer {

	class Scheduler {
	public:
		typedef std::shared_ptr<Scheduler> schedulerPtr;
		typedef Mutex MutexType;

	public:
		Scheduler(size_t threads = 1, bool useCaller = true, const std::string& name = "");
		~Scheduler();

		const std::string& getName() const { return m_Name; }

		static Scheduler* getThis();
		static Fiber* GetMainFiber();  // 协程调度器也是运行在一个协程上的

		void start();
		void stop();

		template<typename FiberOrFunc>
		void schedule(FiberOrFunc ff, int thread = -1) {
			bool needTickle = false;
			{
				MutexType::Lock lock(m_Mtx);
				needTickle = scheduleNoLock(ff, thread);
			}

			if (needTickle)
				tickle();
		}

		template<typename InputIterator>
		void schedule(InputIterator begin, InputIterator end) {
			bool needTickle = false;
			{
				MutexType::Lock lock(m_Mtx);
				while (begin != end) {
					needTickle = scheduleNoLock(&*begin, -1) || needTickle;
					++begin;
				}
			}
			if (needTickle)
				tickle();
		}

	protected:
		virtual void idle();
		virtual bool stopping();
		virtual void tickle();

		void setThis();
		void run();

		bool hasIdleThreads() { return m_IdleThreadCount > 0; }

	private:
		template<typename FiberOrFunc>
		bool scheduleNoLock(FiberOrFunc ff, int thread) {
			bool needTickle = m_Fibers.empty();
			FiberAndThread ft(ff, thread);
			if (ft.fiber || ft.func)
				m_Fibers.push_back(ft);
			return needTickle;
		}

	private:
		struct FiberAndThread {
		public:
			Fiber::fiberPtr fiber;
			std::function<void()> func;
			int thread;  // 使用哪个线程

			FiberAndThread(Fiber::fiberPtr fb, int thr)
				: fiber(fb), thread(thr)
			{
			}

			FiberAndThread(Fiber::fiberPtr* fbptr, int thr)
				: thread(thr)
			{
				fiber.swap(*fbptr);
			}

			FiberAndThread(std::function<void()> f, int thr)
				: thread(thr)
			{
				func.swap(f);
			}

			FiberAndThread(std::function<void()>* f, int thr)
				: thread(thr)
			{
				func.swap(*f);
			}

			FiberAndThread()
				: thread(-1)
			{
			}

			void reset() {
				fiber = nullptr;
				func = nullptr;
				thread = -1;
			}
		};

	private:
		MutexType m_Mtx;
		std::vector<Thread::threadPtr> m_Threads;         // 线程池
		std::vector<FiberAndThread> m_Fibers;             // 协程池
		Fiber::fiberPtr m_RootFiber;                      // 协程调度器运行在哪个协程上
		std::string m_Name;
	
	protected:
		std::vector<int> m_ThreadIds;
		int m_ThreadCount = 0;
		std::atomic<size_t> m_ActiveThreadCount{ 0 };
		std::atomic<size_t> m_IdleThreadCount{ 0 };
		bool m_Stopping = true;
		bool m_AutoStop = false;
		int m_RootThread = 0;
	};

}