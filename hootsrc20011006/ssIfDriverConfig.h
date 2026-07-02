#ifndef __SSIFDRIVERCONFIG_H__
#define __SSIFDRIVERCONFIG_H__

#include "ssIfFolder.h"

class ssIfDriverConfig : public ssIfFolder
{
public:
	virtual int GetOption(const string &_key, int _default = 0) const = 0;
	virtual void AddOption(const string &_key, int _value = 0) = 0;

	virtual void SetDriverType(const string &_type) = 0;
	virtual void SetDriverSubType(const string &_type) = 0;

	virtual string GetDriverType(void) const = 0;
	virtual string GetDriverSubType(void) const = 0;
};

#endif // __SSIFDRIVERCONFIG_H__
