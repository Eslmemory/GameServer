#include "utils.h"
#include "thread.h"

namespace WebServer {

	static thread_local Thread* t_Thread = nullptr;
	static thread_local std::string t_ThreadName = "UNKNOW";

	Thread* Thread::getThis() {
		return t_Thread;
	}

	void Thread::setName(const std::string& name) {
		if (name.empty())
			return;
		if (t_Thread)
			t_Thread->m_Name = name;
		t_ThreadName = name;
	}

	const std::string& Thread::getName() const {
		return t_ThreadName;
	}
	
	Thread::Thread(std::function<void()> func, const std::string& name)
		: m_Func(func), m_Name(name)
	{
		if (name.empty())
			m_Name = "UNKNOW";
		int result = pthread_create(&m_Thread, nullptr, &Thread::run, this);
		if (result) {
			throw std::logic_error("pthread_create error");
		}
		// ȷ���߳��������������ͷ�(run�����notify)
		m_Semaphore.wait();
	}

	Thread::~Thread() {
		// Join����Ϊ0����Ϊ�˺���������ͻ,ʹ��Join�ͱ�ʾ�ȴ��������ͷ�,��������ʲô������,�����ʹ��Join֮ǰ�͵�����������,��ֱ�ӽ��������̴߳����̷߳���
		if (m_Thread)
			pthread_detach(m_Thread);
	}

	void Thread::join() {
		if (m_Thread) {
			int result = pthread_join(m_Thread, nullptr);
			if (result) {
				throw std::logic_error("pthread_join error");
			}
			m_Thread = 0;
		}
	}

	void* Thread::run(void* args)
	{
		Thread* thread = (Thread*)args;
		t_Thread = thread;
		t_ThreadName = thread->m_Name;
		thread->m_Id = GetThreadId();
		pthread_setname_np(pthread_self(), thread->m_Name.substr(0, 15).c_str());

		std::function<void()> func;
		func.swap(thread->m_Func);
		// ׼������,�ͷ�semaphore,���캯����wait�˳�����,���߳̿��Լ������½���
		thread->m_Semaphore.notify();
		func();
		
		return 0;
	}
}