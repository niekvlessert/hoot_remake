#include "StdAfx.h"
#include "ssIfFolder.h"
#include "ssBindConfig.h"

ssBindConfig::ssBindConfig()
{
	m_parent = NULL;
}

ssBindConfig::~ssBindConfig()
{
}

// --------------------------------
// ssIfFolder

ssIfFolder::Type ssBindConfig::GetType(void) const
{
	return ssIfFolder::TYPE_NODE;
}

ssIfFolder *ssBindConfig::GetParent(void) const
{
	return m_parent;
}

void ssBindConfig::SetParent(ssIfFolder *_parent)
{
	m_parent = _parent;
}

int ssBindConfig::GetChildCount(void) const
{
	return 0;
}

ssIfFolder *ssBindConfig::GetChild(int _index) const
{
	return NULL;
}

string ssBindConfig::GetName(void) const
{
	return NULL;
}


// --------------------------------
// ssIfDriverConfig

int ssBindConfig::GetOption(const string &_key, int _default) const
{
	int value = _default;
	ssOption::const_iterator i = m_option.find(_key);
	if (i != m_option.end()) {
		value = i->second;
	}

	return value;
}

void ssBindConfig::AddOption(const string &_key, int _value)
{
	m_option[_key] = _value;
}

void ssBindConfig::SetDriverType(const string &_type)
{
	driver_majortype = _type;
}

void ssBindConfig::SetDriverSubType(const string &_type)
{
	driver_subtype = _type;
}

string ssBindConfig::GetDriverType(void) const
{
	return driver_majortype;
}

string ssBindConfig::GetDriverSubType(void) const
{
	return driver_subtype;
}


// --------------------------------
// ssBindConfig

int ssBindConfig::GetExtCount(void) const
{
	return m_ext.size();;
}

string ssBindConfig::GetExt(int _index) const
{
	if (_index >= m_ext.size()) {
		return NULL;
	}

	return m_ext.at(_index);
}

void ssBindConfig::AddExt(const string &_name)
{
	m_ext.push_back(_name);
}

