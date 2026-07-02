#ifndef __SSTIMER_H__
#define __SSTIMER_H__

typedef bool (*ssTimerHandler)(void *_param);

class ssTimer
{
public:
	ssTimer(int _id = 0);
	virtual ~ssTimer();

	void SetID(int _id);
	int GetID(void) const;

	void Reset(double _t = 0.0);
	void Update();
	double GetNextEventTime(void) const;
	void SetInterval(double _interval, bool _update = false);
	void SetCurrentTime(double _curtime);

	void Active(bool _b);
	bool IsActive() const;

	void SetHandlerBegin(ssTimerHandler _handler, void *_param = NULL);
	bool InvokeHandlerBegin(void) const;
	void SetHandlerEnd(ssTimerHandler _handler, void *_param = NULL);
	bool InvokeHandlerEnd(void) const;

private:
	int m_ID;
	double m_Interval;
	double m_NextEventTime;
	double m_CurrentTime;
	bool m_Active;

	ssTimerHandler m_handler_begin;
	void *m_param_begin;
	ssTimerHandler m_handler_end;
	void *m_param_end;
};

inline void ssTimer::SetID(int _id)
{
	m_ID = _id;
}

inline int ssTimer::GetID(void) const
{
	return m_ID;
}

inline void ssTimer::Reset(double _t)
{
	m_NextEventTime = _t;
	m_CurrentTime = _t;
}

inline void ssTimer::Update(void)
{
	m_NextEventTime += m_Interval;
}

inline double ssTimer::GetNextEventTime(void) const
{
	return m_NextEventTime;
}

inline void ssTimer::SetInterval(double _interval, bool _update)
{
	m_Interval = _interval;
	if (_update) {
		double next = _interval - (m_NextEventTime - m_CurrentTime);
		if (next > 0.0) {
			m_NextEventTime = m_CurrentTime + next;
		} else {
			m_NextEventTime = m_CurrentTime;
		}
	} else {
		m_NextEventTime = m_CurrentTime + m_Interval;
	}
}

inline void ssTimer::SetCurrentTime(double _curtime)
{
	m_CurrentTime = _curtime;
}

inline void ssTimer::Active(bool _b)
{
	if (_b) {
		m_NextEventTime = m_CurrentTime + m_Interval;
	}
	m_Active = _b;
}

inline bool ssTimer::IsActive(void) const
{
	return m_Active;
}

inline void ssTimer::SetHandlerBegin(ssTimerHandler _handler, void *_param)
{
	m_handler_begin = _handler;
	m_param_begin = _param;
}

inline bool ssTimer::InvokeHandlerBegin(void) const
{
	bool ret = true;
	if (m_handler_begin) {
		ret = m_handler_begin(m_param_begin);
	}
	return ret;
}

inline void ssTimer::SetHandlerEnd(ssTimerHandler _handler, void *_param)
{
	m_handler_end = _handler;
	m_param_end = _param;
}

inline bool ssTimer::InvokeHandlerEnd(void) const
{
	bool ret = true;
	if (m_handler_end) {
		ret = m_handler_end(m_param_end);
	}
	return ret;
}

#endif // __SSTIMER_H__
