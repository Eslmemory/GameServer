#pragma once
#include <memory>
#include <functional>
#include <ucontext.h>

namespace WebServer {
	
	class Scheduler;
	
	class Fiber : public std::enable_shared_from_this<Fiber> {
	public:
		friend class Scheduler;
		typedef std::shared_ptr<Fiber> fiberPtr;

		enum State {
			INIT,    // 初始化状态
			HOLD,    // 暂停状态
			EXEC,    // 运行状态
			TERM,    // 结束状态
			READY,   // 可执行状态
			EXCEPT   // 异常状态
		};

	private:
		// 主协程
		Fiber();

	public:
		Fiber(std::function<void()> func, size_t stackSize = 0, bool useCaller = false);
		~Fiber();
		
		void reset(std::function<void()> func);
		
		void swapIn();
		void swapOut();

		void call();
		void back();

		uint64_t getId() const { return m_Id; }
		State getState() const { return m_State; }

	public:
		static void setThis(Fiber* ptr);
		static Fiber::fiberPtr getThis();

		static void YieldToReady();
		static void YieldToHold();

		static void MainFunc();
		static void CallerMainFunc();

		static uint64_t TotalFiber();

	private:
		uint64_t m_Id = 0;
		uint32_t m_StackSize = 0;
		State m_State = INIT;

		ucontext_t m_Context;
		void* m_Stack = nullptr;
		std::function<void()> m_Func;
	};
}