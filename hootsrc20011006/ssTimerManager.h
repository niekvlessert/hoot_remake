#ifndef __SSTIMERMANAGER_H__
#define __SSTIMERMANAGER_H__

#include "ssTimer.h"
#include "ssMutex.h"

class ssTimerManager
{
public:
	ssTimerManager();
	~ssTimerManager();

	bool Register(ssTimer *_timer);
	bool Remove(ssTimer *_timer);
	void ResetAllTimers(void);
	ssTimer *GetNextEvent(void);
	void SetCurrentTime(double _t);

private:
	void Lock(void);
	void Unlock(void);

	ssMutex m_Mutex;
	list<ssTimer *> m_Timers;
};

#endif // __SSTIMERMANAGER_H__
