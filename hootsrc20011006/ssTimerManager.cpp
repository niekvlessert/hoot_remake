#include "StdAfx.h"
#include "ssTimerManager.h"
#include "ssTimer.h"

ssTimerManager::ssTimerManager()
{

}

ssTimerManager::~ssTimerManager()
{
}

void ssTimerManager::Lock(void)
{
	m_Mutex.Lock();
}

void ssTimerManager::Unlock(void)
{
	m_Mutex.Unlock();
}

bool ssTimerManager::Register(ssTimer *_timer)
{
	Lock();

	m_Timers.insert(m_Timers.end(), _timer);

	Unlock();
	return true;
}

bool ssTimerManager::Remove(ssTimer *_timer)
{
	Lock();

	m_Timers.remove(_timer);

	Unlock();
	return true;
}

static void ResetTimer(ssTimer *_timer)
{
	_timer->Reset();
	_timer->Update();
}

void ssTimerManager::ResetAllTimers(void)
{
	for_each(m_Timers.begin(), m_Timers.end(), ::ResetTimer);
}

ssTimer *ssTimerManager::GetNextEvent(void)
{
	Lock();

	ssTimer *ret = NULL;

	ASSERT(m_Timers.size() != 0);

	list<ssTimer *>::const_iterator i;
	for (i = m_Timers.begin(); i != m_Timers.end(); i++) {
		ssTimer *c = *i;
		if (!c->IsActive()) {
			continue;
		}
		if (ret == NULL) {
			ret = c;
		} else if (c->GetNextEventTime() < ret->GetNextEventTime()) {
			ret = c;
		}
	}

	Unlock();

	return ret;
}

void ssTimerManager::SetCurrentTime(double _t)
{
	Lock();

	list<ssTimer *>::const_iterator i;
	for (i = m_Timers.begin(); i != m_Timers.end(); i++) {
		(*i)->SetCurrentTime(_t);
	}

	Unlock();
}
