#include "scheduler.h"
#include "core.h"
#include "utils.h"

namespace WebServer {

	static thread_local Scheduler* s_Scheduler = nullptr;
	static thread_local Fiber* s_SchedulerFiber = nullptr;
	
	// useCaller: �Ƿ񵥶���һ���߳�+Э����Ϊ��ȡ����ר��
	Scheduler::Scheduler(size_t threads, bool useCaller, const std::string& name)
		: m_Name(name)
	{
		WS_ASSERT(threads > 0);

		if (useCaller) {
			WebServer::Fiber::getThis();
			--threads;

			WS_ASSERT(getThis() == nullptr);
			s_Scheduler = this;

			m_RootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
			Thread::setName(m_Name);

			s_SchedulerFiber = m_RootFiber.get();
			m_RootThread = GetThreadId();

			m_ThreadIds.push_back(m_RootThread);
		}
		else {
			m_RootThread = -1;
		}
		m_ThreadCount = threads;
	}

	Scheduler::~Scheduler() {
		WS_ASSERT(m_Stopping);
		if (this == getThis())
			s_Scheduler = nullptr;
	}

	Scheduler* Scheduler::getThis() {
		return s_Scheduler;
	}

	void Scheduler::setThis() {
		s_Scheduler = this;
	}

	Fiber* Scheduler::GetMainFiber() {
		return s_SchedulerFiber;
	}

	bool Scheduler::stopping() {
		MutexType::Lock lock(m_Mtx);
		return m_AutoStop && m_Stopping && m_Fibers.empty() && m_ActiveThreadCount == 0;
	}

	void Scheduler::stop() {
		m_AutoStop = true;
		if (m_RootFiber && m_ThreadCount == 0 && 
			(m_RootFiber->getState() == Fiber::INIT || m_RootFiber->getState() == Fiber::TERM)) {
			m_Stopping = true;

			if (stopping())
				return;
		}

		m_Stopping = true;
		for (size_t i = 0; i < m_ThreadCount; i++)
			tickle();

		if (m_RootFiber)
			tickle();

		// ��Ϊ֮ǰrootFiberû�б�����,�о�����rootFiber������ɨβ��
		if (m_RootFiber) {
			if (!stopping())
				m_RootFiber->call();
		}

		std::vector<Thread::threadPtr> thrs;
		{
			MutexType::Lock lock(m_Mtx);
			thrs.swap(m_Threads);
		}

		for (auto& i : thrs) {
			i->join();
		}
	}

	void Scheduler::start() {
		MutexType::Lock lock(m_Mtx);
		if (!m_Stopping)                 // m_Stopping=false�����Ѿ�������
			return;
		m_Stopping = true;
		WS_ASSERT(m_Threads.empty());

		m_Threads.resize(m_ThreadCount);
		for (size_t i = 0; i < m_ThreadCount; i++) {
			m_Threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_Name + "_" + std::to_string(i)));
			m_ThreadIds.push_back(m_Threads[i]->getId());
		}
		lock.unlock();
	}

	void Scheduler::idle() {
		printf("idle\n");
		while (!stopping())
			Fiber::YieldToHold();
	}

	void Scheduler::tickle() {
		printf("tickle\n");
	}

	// Э�̵��������ڵ��̵߳�run������û�е��õ�
	void Scheduler::run() {
		setThis();
		if (GetThreadId() != m_RootThread)
			s_SchedulerFiber = Fiber::getThis().get();

		Fiber::fiberPtr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
		Fiber::fiberPtr funcFiber;

		FiberAndThread ft;
		while (true) {
			ft.reset();
			bool tickleMe = false;
			bool isActive = false;

			{
				MutexType::Lock lock(m_Mtx);
				auto it = m_Fibers.begin();
				while (it != m_Fibers.end()) {
					// Ӧ���������߳��ϵ��õ�Э��
					if (it->thread != -1 && it->thread != GetThreadId()) {
						++it;
						tickleMe = true;
						continue;
					}

					// Э����������
					WS_ASSERT(it->fiber || it->func);
					if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
						it++;
						continue;
					}

					// ȡ��Э��ִ��
					ft = *it;
					m_Fibers.erase(it++);
					++m_ActiveThreadCount;
					isActive = true;
					break;
				}
				// û�е����һ��,�����ɹ�ȡ��,������һ��
				tickleMe |= (it != m_Fibers.end());
			}

			if (tickleMe)
				tickle();

			if (ft.fiber && (ft.fiber->getState() != Fiber::TERM) && ft.fiber->getState() != Fiber::EXCEPT) {
				ft.fiber->swapIn();
				--m_ActiveThreadCount;

				if (ft.fiber->getState() == Fiber::READY) {
					schedule(ft.fiber); // ���ִ���껹��READY״̬,���ٴμ���m_Fibers
				}
				else if(ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT) {
					ft.fiber->m_State = Fiber::HOLD;
				}
				ft.reset();
			}
			else if (ft.func) {
				if (funcFiber)
					funcFiber->reset(ft.func);
				else
					funcFiber.reset(new Fiber(ft.func));
				ft.reset();
				funcFiber->swapIn();
				--m_ActiveThreadCount;
				if (funcFiber->getState() == Fiber::READY) {
					schedule(funcFiber);
					funcFiber.reset();
				}
			}
			else {
				if (isActive) {
					--m_ActiveThreadCount;
					continue;
				}
				if (idleFiber->getState() == Fiber::TERM) {
					printf("idle fiber term\n");
					break;
				}
				++m_IdleThreadCount;
				idleFiber->swapIn();
				--m_IdleThreadCount;
				if ((idleFiber->getState() != Fiber::TERM) && (idleFiber->getState() != Fiber::EXCEPT)) {
					idleFiber->m_State = Fiber::HOLD;
				}
			}
		}

	}

}