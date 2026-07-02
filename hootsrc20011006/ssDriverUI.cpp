#include "StdAfx.h"
#include "ssDriverUI.h"

#include "ssSoundDriverManager.h"


ssDriverUI::ssDriverUI()
{
	m_bTitleSelectMode = true;
	m_CurrentTitleNo = 0;
	m_CurrentCallNo = 0;
	m_CurrentGameNo = 0;
}

ssDriverUI::~ssDriverUI()
{
}

bool ssDriverUI::IsTitleSelectMode(void) const
{
	return m_bTitleSelectMode;
}

int ssDriverUI::GetMaxTitleNo(void) const
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

	return cfglist[m_CurrentGameNo]->titlelist.size();
}

int ssDriverUI::GetCurrentTitleNo(void) const
{
	return m_CurrentTitleNo;
}

void ssDriverUI::SetCurrentTitleNo(int _no)
{
	int size = GetMaxTitleNo();
	if (_no < 0) {
		_no = 0;
	} else if (_no >= size) {
		_no = size - 1;
	}
	m_CurrentTitleNo = _no;
}

int ssDriverUI::GetCurrentCallNo(void) const
{
	return m_CurrentCallNo;
}

void ssDriverUI::SetCurrentCallNo(int _no)
{
	m_CurrentCallNo = _no;
}

int ssDriverUI::GetMaxGameNo(void) const
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

	return cfglist.size();
}

int ssDriverUI::GetCurrentGameNo(void) const
{
	return m_CurrentGameNo;
}

void ssDriverUI::SetCurrentGameNo(int _no)
{
	int size = GetMaxGameNo();
	if (_no < 0) {
		_no = 0;
	} else if (_no >= size) {
		_no = size - 1;
	}
	m_CurrentGameNo = _no;
	SetCurrentTitleNo(0);
}

int ssDriverUI::GetSelectedCallNo(void) const
{
	int ret;

	if (IsTitleSelectMode()) {
		ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
		const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

		int t = GetCurrentTitleNo();
		ret = cfglist[m_CurrentGameNo]->titlelist[t].code;
	} else {
		ret = GetCurrentCallNo();
	}
	return ret;
}

const ssDriverConfig::ssTitle &ssDriverUI::GetSelectedTitle(void) const
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

	int t = GetCurrentTitleNo();
	return cfglist[m_CurrentGameNo]->titlelist[t];
}

const ssDriverConfig &ssDriverUI::GetSelectedDriver(void) const
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

	return *cfglist[m_CurrentGameNo];
}

void ssDriverUI::LoadDriver(void)
{
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssDriverConfigList &cfglist = psdm->GetDriverConfig();

	psdm->LoadDriver(cfglist[GetCurrentGameNo()]);
}
