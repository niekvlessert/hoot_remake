#include "StdAfx.h"
#include "ssSoundDriverManager.h"
#include "ssUnZip.h"
#include "ssDriverConfig.h"


ssDriverConfig::ssDriverConfig(bool _singlefile)
{
	singlefile = _singlefile;
	base_config = NULL;
}

ssDriverConfig::~ssDriverConfig()
{
}

// --------------------------------
// ssIfFolder

ssIfFolder::Type ssDriverConfig::GetType(void) const
{
	return ssIfFolder::TYPE_NODE;
}

ssIfFolder *ssDriverConfig::GetParent(void) const
{
	return m_parent;
}

void ssDriverConfig::SetParent(ssIfFolder *_parent)
{
	m_parent = _parent;
}

int ssDriverConfig::GetChildCount(void) const
{
	return 0;
}

ssIfFolder *ssDriverConfig::GetChild(int _index) const
{
	return NULL;
}

string ssDriverConfig::GetName(void) const
{
	return name;
}

// --------------------------------
// ssIfDriverConfig

int ssDriverConfig::GetOption(const string &_key, int _default) const
{
	int value = _default;

	if (base_config == NULL) {
		ssOption::const_iterator i = option.find(_key);
		if (i != option.end()) {
			value = i->second;
		}
	} else {
		value = base_config->GetOption(_key, _default);
	}

	return value;
}

void ssDriverConfig::AddOption(const string &_key, int _value)
{
	option[_key] = _value;
}

void ssDriverConfig::SetDriverType(const string &_type)
{
	driver_majortype = _type;
}

void ssDriverConfig::SetDriverSubType(const string &_type)
{
	driver_subtype = _type;
}

string ssDriverConfig::GetDriverType(void) const
{
	string ret;

	if (base_config == NULL) {
		ret = driver_majortype;
	} else {
		ret = base_config->GetDriverType();
	}

	return ret;
}

string ssDriverConfig::GetDriverSubType(void) const
{
	string ret;

	if (base_config == NULL) {
		ret = driver_subtype;
	} else {
		ret = base_config->GetDriverSubType();
	}

	return ret;
}


// --------------------------------
// ssDriverConfig

void ssDriverConfig::SetBaseConfig(ssIfDriverConfig *_config)
{
	base_config = _config;

	// for backward compatibility
	driver_majortype = base_config->GetDriverType();
	driver_subtype = base_config->GetDriverSubType();
}

bool ssDriverConfig::IsSingleFile(void) const
{
	return singlefile;
}

void ssDriverConfig::SetFile(const string &_file)
{
	archive = _file;
}

void ssDriverConfig::ClearTitle(void)
{
	titlelist.clear();
}

void ssDriverConfig::AddTitle(DWORD _code, const string &_title)
{
	ssTitle title;

	title.code = _code;
	title.name = _title;

	titlelist.push_back(title);
}
