#ifndef __SSBINDCONFIG_H__
#define __SSBINDCONFIG_H__

#include "ssIfDriverConfig.h"

class ssBindConfig : public ssIfDriverConfig
{
public:
	ssBindConfig();
	~ssBindConfig();

public:
	// --------------------------------
	// ssIfFolder

	ssIfFolder::Type GetType(void) const;

	ssIfFolder *GetParent(void) const;
	void SetParent(ssIfFolder *_parent);

	int GetChildCount(void) const;
	ssIfFolder *GetChild(int _index) const;

	string GetName(void) const;

public:
	// --------------------------------
	// ssIfDriverConfig

	int GetOption(const string &_key, int _default = 0) const;
	void AddOption(const string &_key, int _value = 0);

	void SetDriverType(const string &_type);
	void SetDriverSubType(const string &_type);

	string GetDriverType(void) const;
	string GetDriverSubType(void) const;

public:
	// --------------------------------
	// ssBindConfig

	int GetExtCount(void) const;
	string GetExt(int _index) const;
	void AddExt(const string &_ext);

private:
	ssIfFolder *m_parent;

	string m_name;
	string m_file;

	string driver_majortype;
	string driver_subtype;

	typedef map<string, int> ssOption;

	ssOption m_option;
	vector<string> m_ext;
};

#endif // __SSBINDCONFIG_H__
