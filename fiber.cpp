#include "core.h"
#include "fiber.h"
#include "scheduler.h"
#include "utils.h"

#include <atomic>

namespace WebServer {

	static size_t s_StackSize = 10000;
	static std::atomic<uint64_t> s_FiberId{0};
	static std::atomic<uint64_t> s_FiberCount{0};

	static thread_local Fiber* s_Fiber = nullptr;
	static thread_local Fiber::fiberPtr s_ThreadFiber = nullptr;

	class MallocStackAllocator {
	public:
		static void* Alloc(size_t size) {
			return malloc(size);
		}

		static void Dealloc(void* ptr, size_t size) {
			return free(ptr);
		}
	};

	using StackAllocator = MallocStackAllocator;

	Fiber::Fiber() {
		m_State = EXEC;
		setThis(this);

		// ����̵߳�������,���㵽ʱ���л������߳���,����û�����ö�ջ,���ظ�������ʱӦ���ǻص����̵߳���Fiber�ĵط�
		// �����ʹ��caller���ǻᵥ����һ����ջ,��Ϊ����Э�̷��صĵط�
		if (getcontext(&m_Context))
			WS_ASSERT_WITHPARAM(false, "m_Context");

		++s_FiberCount;
		printf("Thread %d: Fiber::Fiber main\n", GetThreadId());
	}

	Fiber::Fiber(std::function<void()> func, size_t stackSize, bool useCaller) 
		: m_Func(func), m_Id(++s_FiberId)
	{
		++s_FiberCount;
		// TODO: set value by config
		m_StackSize = stackSize ? stackSize : s_StackSize;

		m_Stack = StackAllocator::Alloc(m_StackSize);
		if(getcontext(&m_Context))
			WS_ASSERT_WITHPARAM(false, "m_Context");

		m_Context.uc_link = nullptr;
		m_Context.uc_stack.ss_sp = m_Stack;
		m_Context.uc_stack.ss_size = m_StackSize;

		if (!useCaller)
			makecontext(&m_Context, &Fiber::MainFunc, 0);
		else
			makecontext(&m_Context, &Fiber::CallerMainFunc, 0);

		printf("Fiber::Fiber id=%d \n", m_Id);
	}

	Fiber::~Fiber() {
		--s_FiberCount;
		if (m_Stack) {
			WS_ASSERT(m_State == INIT || m_State == TERM || m_State == EXCEPT);
			StackAllocator::Dealloc(m_Stack, m_StackSize);
		}
		else {
			WS_ASSERT_WITHPARAM(!m_Func, "~Fiber: m_Func\n");
			WS_ASSERT_WITHPARAM(m_State == EXEC, "~Fiber: current state is exec\n");
			Fiber* cur = s_Fiber;
			if (cur == this)
				setThis(nullptr);
		}
		printf("Fiber::~Fiber id=%d, total=%d \n", m_Id, s_FiberCount.load());
	}

	void Fiber::reset(std::function<void()> func) {
		WS_ASSERT(m_Stack);
		WS_ASSERT(m_State == TERM || m_State == EXCEPT || m_State == INIT);
		m_Func = func;
		if (getcontext(&m_Context))
			WS_ASSERT_WITHPARAM(false, "getcontext");

		m_Context.uc_link = nullptr;
		m_Context.uc_stack.ss_sp = m_Stack;
		m_Context.uc_stack.ss_size = m_StackSize;

		makecontext(&m_Context, &Fiber::MainFunc, 0);
		m_State = INIT;
	}

	void Fiber::swapIn() {
		setThis(this);
		WS_ASSERT(m_State != EXEC);
		m_State = EXEC;
		if (swapcontext(&(Scheduler::GetMainFiber()->m_Context), &m_Context)) {
			WS_ASSERT_WITHPARAM(false, "swapcontext");
		}
	}
	
	void Fiber::swapOut() {
		setThis(Scheduler::GetMainFiber());
		if (swapcontext(&m_Context, &(Scheduler::GetMainFiber()->m_Context)))
			WS_ASSERT_WITHPARAM(false, "swapcontext");
	}

	void Fiber::call() {
		setThis(this);
		m_State = EXEC;
		if (swapcontext(&s_ThreadFiber->m_Context, &m_Context))
			WS_ASSERT_WITHPARAM(false, "swapcontext");
	}

	void Fiber::back() {
		setThis(s_ThreadFiber.get());
		if (swapcontext(&m_Context, &s_ThreadFiber->m_Context)) {
			WS_ASSERT_WITHPARAM(false, "swapcontext");
		}
	}
		
	void Fiber::YieldToReady() {
		Fiber::fiberPtr cur = getThis();
		WS_ASSERT(cur->getState() == EXEC);
		cur->swapOut();
	}

	void Fiber::YieldToHold() {
		Fiber::fiberPtr cur = getThis();
		WS_ASSERT(cur->getState() == EXEC);
		cur->m_State = READY;
		cur->swapOut();
	}

	void Fiber::setThis(Fiber* ptr) {
		s_Fiber = ptr;
	}

	Fiber::fiberPtr Fiber::getThis() {
		if (s_Fiber != nullptr) {
			return s_Fiber->shared_from_this();
		}
		
		Fiber::fiberPtr mainFiber(new Fiber);
		// new Fiber�лὫs_Fiber��ΪmainFiber
		WS_ASSERT((s_Fiber == mainFiber.get()));
		s_ThreadFiber = mainFiber;
		return s_Fiber->shared_from_this();
	}

	uint64_t Fiber::TotalFiber() {
		return s_FiberCount;
	}

	// MainFunc��һ����̬����,���������κ�һ��Fiberʵ��,�����MainFun����ʹ��getThis�൱�����ҵ�ǰ���ĸ�Fiber�����õ���t_Fiber��
	void Fiber::MainFunc() {
		Fiber::fiberPtr cur = getThis();
		WS_ASSERT(cur);

		try {
			cur->m_Func();
			cur->m_Func = nullptr;
			cur->m_State = TERM;
		}
		catch(std::exception& except) {
			cur->m_State = EXCEPT;
			std::cout << "Fiber Except: " << "fiber_id=" << cur->getId() << std::endl;
			WS_ASSERT(false);
		}
		catch (...) {
			cur->m_State = EXCEPT;
			std::cout << "Fiber Except: " << "fiber_id=" << cur->getId() << std::endl;
		}

		auto ptr = cur.get();
		cur.reset(); // �ͷ�cur,�൱�ڽ��ptr��cur,���ǲ�����˵�ͷ���ָ��ָ����ڴ�ռ�,ֻ�ǽ�����ָ�����Ķ������߳�ȥ��
		ptr->swapOut();

		// ���ܵ�����ط�,�������swapOutû�гɹ��˳�
		WS_ASSERT_WITHPARAM(false, "never reach fiber_id=" + std::to_string(ptr->getId()) + " (do not reset this fiber)");
	}

	void Fiber::CallerMainFunc() {
		Fiber::fiberPtr cur = getThis();
		WS_ASSERT(cur);
		try {
			cur->m_Func();
			cur->m_Func = nullptr;
			cur->m_State = TERM;
		}
		catch (std::exception& except) {
			cur->m_State = EXCEPT;
			std::cout << "Fiber Except: " << "fiber_id=" << cur->getId() << std::endl;
			WS_ASSERT(false);
		}
		catch (...) {
			cur->m_State = EXCEPT;
			std::cout << "Fiber Except: " << "fiber_id=" << cur->getId() << std::endl;
		}

		auto ptr = cur.get();
		cur.reset();
		ptr->back();

		// ���ܵ�����ط�,�������swapOutû�гɹ��˳�
		WS_ASSERT_WITHPARAM(false, "never reach fiber_id=" + std::to_string(ptr->getId()));
	}

}