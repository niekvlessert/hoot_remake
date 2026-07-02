#ifndef __SSFMTIMER_H__
#define __SSFMTIMER_H__

#include "ssTimer.h"

class ssFMTimer
{
public:
	ssFMTimer();
	virtual ~ssFMTimer();

protected:
	void InitTimer(int _idTimerA, int _idTimerB);
	void SetClock(double _clock);
	void SetTimerA(BYTE _addr, BYTE _data);
	void SetTimerB(BYTE _data);
	void SetTimerControl(BYTE _data);

	void SetStatus(BYTE _flag);
	void ResetStatus(BYTE _flag);
	BYTE GetStatus(void);

private:
	static bool TimerAHandler(void *_param);
	static bool TimerBHandler(void *_param);

private:
	ssTimer *m_TimerA;
	ssTimer *m_TimerB;

	double m_clock;

	double m_taint;
	double m_tbint;

	bool m_irq;

	bool m_taenable;
	bool m_tbenable;

	BYTE m_status;

	BYTE m_regtc;
	BYTE m_regta[2];
};

#endif // __SSFMTIMER_H__
