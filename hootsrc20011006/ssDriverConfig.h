#ifndef __SSDRIVERCONFIG_H__
#define __SSDRIVERCONFIG_H__

#include "ssIfDriverConfig.h"

class ssDriverConfig;
typedef vector<ssDriverConfig *> ssDriverConfigList;

class ssDriverConfig : public ssIfDriverConfig
{
public:
	struct ssRom
	{
		string type;
		DWORD offset;
		string filename;
	};
	struct ssTitle
	{
		DWORD code;
		string name;
	};

	typedef map<string, int> ssOption;
	typedef vector<ssRom> ssRomList;
	typedef vector<ssTitle> ssTitleList;

public:
	ssDriverConfig(bool _singlefile = false);
	~ssDriverConfig();

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
	void CopyOption(const ssIfDriverConfig *_config);

	void SetDriverType(const string &_type);
	void SetDriverSubType(const string &_type);

	string GetDriverType(void) const;
	string GetDriverSubType(void) const;

public:
	// --------------------------------
	// ssDriverConfig

	void SetBaseConfig(ssIfDriverConfig *_config);
	bool IsSingleFile(void) const;

	void SetFile(const string &_file);

	void ClearTitle(void);
	void AddTitle(DWORD _code, const string &_title);

public:
	string name;
	string driver_majortype;
	string driver_subtype;
	string archive;

	ssOption option;
	ssRomList romlist;
	ssTitleList titlelist;

private:
	ssIfFolder *m_parent;

	bool singlefile;
	ssIfDriverConfig *base_config;
};

#endif // __SSDRIVERCONFIG_H__
