#pragma once

#include "mutex.h"
#include <set>
#include <vector>
#include <memory>
#include <functional>

namespace WebServer {

	class TimerManager;

	class Timer : public std::enable_shared_from_this<Timer> {
	
	friend class TimerManager;
	public:
		typedef std::shared_ptr<Timer> timerPtr;

		bool cancel();
		bool refresh();
		bool reset(uint64_t ms, bool fromNow);

	private:
		Timer(uint64_t ms, std::function<void()> func, bool recurring, TimerManager* manager);

		Timer(uint64_t next);

	private:
		bool m_Recurring = false;
		uint64_t m_Ms = 0;
		uint64_t m_Next = 0;
		std::function<void()> m_Func;
		TimerManager* m_Manager = nullptr;

	private:
		// 定时器比较仿函数
		struct Comparator {
			bool operator() (const Timer::timerPtr& lhs, const Timer::timerPtr& rhs) const;
		};
	};

	class TimerManager {
	
	friend class Timer;
	public:
		typedef RWMutex RWMutexType;

		TimerManager();
		virtual ~TimerManager();

		Timer::timerPtr addTimer(uint64_t ms, std::function<void()> func, bool recurring = false);

		Timer::timerPtr addConditionTimer(uint64_t ms, std::function<void()> func, std::weak_ptr<void> weakCond, bool recurring = false);

		uint64_t getNextTimer();
		void listExpiredFunc(std::vector<std::function<void()>>& funcs);

		bool hasTimer();
	protected:
		virtual void onTimerInsertedAtFront() = 0;
		void addTimer(Timer::timerPtr ptr, RWMutexType::WriteLock& lock);
	private:
		bool detectClockRollover(uint64_t nowMs);
	private:
		std::set<Timer::timerPtr, Timer::Comparator> m_Timers;
		RWMutexType m_Mtx;
		// 是否触发onTimerInsertedAtFront
		bool m_Tickled = false;
		uint64_t m_previouseTime = 0;
	};
}