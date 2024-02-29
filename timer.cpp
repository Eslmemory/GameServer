#include "timer.h"
#include "utils.h"

namespace WebServer {

	// 默认从小到大
	bool Timer::Comparator::operator()(const Timer::timerPtr& lhs, const Timer::timerPtr& rhs) const {
		if (!lhs && !rhs)
			return false;
		if (!lhs)
			return true;
		if (!rhs)
			return false;
		if (lhs->m_Next < rhs->m_Next)
			return false;
		if (lhs->m_Next > rhs->m_Next)
			return true;
		return lhs.get() < rhs.get();
	}

	Timer::Timer(uint64_t ms, std::function<void()> func, bool recurring, TimerManager* manager) 
		: m_Ms(ms), m_Func(func), m_Recurring(recurring), m_Manager(manager)
	{
		m_Next = GetCurrentMS() + m_Ms;
	}

	Timer::Timer(uint64_t next)
		: m_Next(next)
	{
	}

	bool Timer::cancel() {
		TimerManager::RWMutexType::WriteLock lock(m_Manager->m_Mtx);
		if (m_Func) {
			m_Func = nullptr;
			auto it = m_Manager->m_Timers.find(shared_from_this());
			m_Manager->m_Timers.erase(it);
			return true;
		}
		return false;
	}

	bool Timer::refresh() {
		TimerManager::RWMutexType::WriteLock lock(m_Manager->m_Mtx);
		if (!m_Func)
			return false;

		auto it = m_Manager->m_Timers.find(shared_from_this());
		if (it == m_Manager->m_Timers.end())
			return false;
		m_Manager->m_Timers.erase(it);
		m_Next = GetCurrentMS() + m_Ms;
		m_Manager->m_Timers.insert(shared_from_this());
		return true;
	}

	bool Timer::reset(uint64_t ms, bool fromNow) {
		if (ms == m_Ms && !fromNow)
			return true;
		TimerManager::RWMutexType::WriteLock lock(m_Manager->m_Mtx);

		if (!m_Func)
			return false;
		auto it = m_Manager->m_Timers.find(shared_from_this());
		if (it == m_Manager->m_Timers.end())
			return false;
		m_Manager->m_Timers.erase(it);
		uint64_t start = 0;
		if (fromNow)
			start = GetCurrentMS();
		else
			start = m_Next - m_Ms;
		m_Ms = ms;
		m_Next = start + m_Ms;
		m_Manager->addTimer(shared_from_this(), lock);
		return true;
	}

	TimerManager::TimerManager() {
		m_previouseTime = GetCurrentMS();
	}

	TimerManager::~TimerManager() {

	}

	Timer::timerPtr TimerManager::addTimer(uint64_t ms, std::function<void()> func, bool recurring) {
		Timer::timerPtr timer(new Timer(ms, func, recurring, this));
		RWMutexType::WriteLock lock(m_Mtx);
		addTimer(timer, lock);
		return timer;
	}

	void TimerManager::addTimer(Timer::timerPtr ptr, RWMutexType::WriteLock& lock) {
		auto it = m_Timers.insert(ptr).first;
		bool atFront = (it == m_Timers.begin()) && !m_Tickled;
		if (atFront)
			m_Tickled = true;
		lock.unlock();
		if (atFront)
			onTimerInsertedAtFront();
	}

	static void OnTimer(std::weak_ptr<void> weakCond, std::function<void()> func) {
		std::shared_ptr<void> tmp = weakCond.lock();
		if (tmp)
			func();
	}

	Timer::timerPtr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> func, std::weak_ptr<void> weakCond, bool recurring) {
		return addTimer(ms, std::bind(&OnTimer, weakCond, func), recurring);
	}

	uint64_t TimerManager::getNextTimer() {
		RWMutexType::ReadLock lock(m_Mtx);
		m_Tickled = false;
		if (m_Timers.empty())
			return ~0ull;
		const Timer::timerPtr& next = *m_Timers.begin();
		uint64_t nowMs = GetCurrentMS();
		if (nowMs >= next->m_Next)
			return 0;
		else
			return next->m_Next - nowMs;
	}

	bool TimerManager::detectClockRollover(uint64_t nowMs) {
		bool rollover = false;
		if (nowMs < m_previouseTime && nowMs < (m_previouseTime - 60 * 60 * 1000))
			rollover = true;
		m_previouseTime = nowMs;
		return rollover;
	}

	void TimerManager::listExpiredFunc(std::vector<std::function<void()>>& funcs) {
		uint64_t nowMs = GetCurrentMS();
		std::vector<Timer::timerPtr> expired;
		{
			RWMutexType::ReadLock lock(m_Mtx);
			if (m_Timers.empty())
				return;
		}
		RWMutexType::WriteLock lock(m_Mtx);
		if (m_Timers.empty())
			return;
		bool rollover = detectClockRollover(nowMs);
		if (!rollover && ((*m_Timers.begin())->m_Next > nowMs))
			return;

		// 筛选出小于等于当前时间戳的定时器事件
		Timer::timerPtr nowTimer(new Timer(nowMs));
		auto it = rollover ? m_Timers.end() : m_Timers.lower_bound(nowTimer);
		while (it != m_Timers.end() && (*it)->m_Next == nowMs) {
			++it;
		}

		expired.insert(expired.begin(), m_Timers.begin(), it);
		m_Timers.erase(m_Timers.begin(), it);
		funcs.reserve(expired.size());

		for (auto& timer : expired) {
			funcs.push_back(timer->m_Func);
			if (timer->m_Recurring) {
				timer->m_Next = nowMs + timer->m_Ms;
				m_Timers.insert(timer);
			}
			else
				timer->m_Func = nullptr;
		}
	}

	bool TimerManager::hasTimer() {
		RWMutexType::ReadLock lock(m_Mtx);
		return !m_Timers.empty();
	}

}