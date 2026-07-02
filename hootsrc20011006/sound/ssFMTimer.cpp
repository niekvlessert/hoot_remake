#include "StdAfx.h"
#include "sound/ssFMTimer.h"

ssFMTimer::ssFMTimer()
{
	m_TimerA = NULL;
	m_TimerB = NULL;

	m_regta[0] = 0;
	m_regta[1] = 0;
	m_regtc = 0;
	m_clock = 4000000;

	m_status = 0;

	m_irq = false;
	m_taenable = false;
	m_tbenable = false;
}

ssFMTimer::~ssFMTimer()
{
	delete m_TimerA;
	delete m_TimerB;
}

bool ssFMTimer::TimerAHandler(void *_param)
{
	bool ret = false;
	ssFMTimer *timer = (ssFMTimer *)_param;

	if (timer->m_taenable) {
		timer->SetStatus(1);
		if (timer->m_regtc & 0x04) {
			ret = true;
		}
	}

	return ret;
}

bool ssFMTimer::TimerBHandler(void *_param)
{
	bool ret = false;

	ssFMTimer *timer = (ssFMTimer *)_param;
	if (timer->m_tbenable) {
		timer->SetStatus(2);
		if (timer->m_regtc & 0x08) {
			ret = true;
		}
	}

	return ret;
}

void ssFMTimer::InitTimer(int _idTimerA, int _idTimerB)
{
	m_TimerA = new ssTimer(_idTimerA);
	m_TimerA->SetHandlerBegin(TimerAHandler, this);
	m_TimerA->Active(true);
	m_TimerB = new ssTimer(_idTimerB);
	m_TimerB->SetHandlerBegin(TimerBHandler, this);
	m_TimerB->Active(true);
}

void ssFMTimer::SetClock(double _clock)
{
	m_clock = (double)_clock;
}

void ssFMTimer::SetTimerA(BYTE _addr, BYTE _data)
{
	DWORD tmp;
	m_regta[_addr & 1] = _data;
	tmp = (m_regta[0] << 2) + (m_regta[1] & 3);
	//m_taint = (double)(1024-tmp) * 64. / m_clock;
	m_taint = (double)(1024-tmp) * m_clock;
	m_TimerA->SetInterval(m_taint);
}

void ssFMTimer::SetTimerB(BYTE _data)
{
	//m_tbint = (double)(256-_data) * 1024. / m_clock;
	m_tbint = (double)((256-_data) * 16) * m_clock;
	m_TimerB->SetInterval(m_tbint);
}

void ssFMTimer::SetTimerControl(BYTE _data)
{
	BYTE tmp = m_regtc ^ _data;
	m_regtc = _data;

	if (_data & 0x10) {
		ResetStatus(1);
	}
	if (_data & 0x20) {
		ResetStatus(2);
	}

	if (tmp & 0x01) {
		if (_data & 0x01) {
			//m_TimerA->Active(true);
			m_taenable = true;
		} else {
			//m_TimerA->Active(false);
			m_taenable = false;;
		}
	}
	if (tmp & 0x02) {
		if (_data & 0x02) {
			//m_TimerB->Active(true);
			m_tbenable = true;
		} else {
			//m_TimerB->Active(false);
			m_tbenable = false;
		}
	}
}

void ssFMTimer::SetStatus(BYTE _flag)
{
	m_status |= _flag;
	if (!m_irq && m_status) {
		m_irq = true;
	}
}

void ssFMTimer::ResetStatus(BYTE _flag)
{
	m_status &= ~_flag;
	if (m_irq && m_status == 0) {
		m_irq = false;
	}
}

BYTE ssFMTimer::GetStatus(void)
{
	return m_status;
}
