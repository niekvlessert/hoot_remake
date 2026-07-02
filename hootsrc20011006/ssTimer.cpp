#include "StdAfx.h"
#include "ssSoundDriverManager.h"
#include "ssTimer.h"

ssTimer::ssTimer(int _id)
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();

	m_ID = _id;
	m_CurrentTime = 0.0;
	m_NextEventTime = 0.0;
	m_Interval = 0.1;

	m_handler_begin = NULL;
	m_param_begin = NULL;
	m_handler_end = NULL;
	m_param_end = NULL;

	Active(true);

	psdm->RegistTimer(this);
}

ssTimer::~ssTimer()
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();

	psdm->RemoveTimer(this);
}
