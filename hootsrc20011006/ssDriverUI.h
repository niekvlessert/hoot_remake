#ifndef __SSDRIVERUI_H__
#define __SSDRIVERUI_H__

#include "ssDriverConfig.h"

class ssDriverUI
{
public:
	ssDriverUI();
	~ssDriverUI();

	bool IsTitleSelectMode(void) const;

	int GetMaxTitleNo(void) const;
	int GetCurrentTitleNo(void) const;
	void SetCurrentTitleNo(int _no);

	int GetCurrentCallNo(void) const;
	void SetCurrentCallNo(int _no);

	int GetMaxGameNo(void) const;
	int GetCurrentGameNo(void) const;
	void SetCurrentGameNo(int _no);

	int GetSelectedCallNo(void) const;
	const ssDriverConfig::ssTitle &GetSelectedTitle(void) const;
	const ssDriverConfig &GetSelectedDriver(void) const;

	void LoadDriver(void);

private:
	bool m_bTitleSelectMode;
	int m_CurrentTitleNo;
	int m_CurrentCallNo;
	int m_CurrentGameNo;
};

#endif // __SSDRIVERUI_H__
