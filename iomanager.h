#pragma once
#include "scheduler.h"
#include "timer.h"

namespace WebServer {

	class IOManager : public Scheduler, public TimerManager {
	public:
		typedef std::shared_ptr<IOManager> ioManagerPtr;
		typedef RWMutex RWMutexType;

		enum Event {
			NONE =  0x0,
			READ =  0x1,
			WRITE = 0x4
		};
		
	public:
		IOManager(size_t threads = 1, bool useCaller = true, const std::string& name = "");
		~IOManager();

		int addEvent(int fd, Event event, std::function<void()> func = nullptr);
		bool delEvent(int fd, Event event);
		bool cancelEvent(int fd, Event event);
		bool cancelAll(int fd);

		static IOManager* getThis();

	protected:
		void idle() override;
		bool stopping() override;
		void tickle() override;
		void onTimerInsertedAtFront() override;

		void contextResize(size_t size);
		bool stopping(uint64_t& timeout);
		
	private:
		struct FdContext {
			typedef Mutex MutexType;

			struct EventContext {
				Scheduler* scheduler = nullptr;
				Fiber::fiberPtr fiber;
				std::function<void()> func;
			};

			EventContext& getContext(Event event);
			void resetContext(EventContext& context);
			void triggerEvent(Event event);
			
			/// ���¼�������
			EventContext read;
			/// д�¼�������
			EventContext write;
			/// �¼������ľ��
			int fd = 0;
			/// ��ǰ���¼�
			Event events = NONE;
			/// �¼���Mutex
			MutexType mtx;
		};

		int m_EpollFd = 0;
		int m_TickleFds[2];
		std::atomic<size_t> m_WaitingEventCount{ 0 }; // ��ǰ�ȴ�ִ�е��¼�����
		RWMutexType m_Mtx;
	public:
		std::vector<FdContext*> m_FdContexts;
	};
}