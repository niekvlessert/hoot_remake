#ifndef __SSDRIVERBINDER_H__
#define __SSDRIVERBINDER_H__

#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssSoundDriver.h"

typedef ssSoundDriver *(*ssDriverFactoryFunction)(const ssDriverConfig *);

class ssDriverRegister;

class ssDriverBinder
{
	friend class ssDriverRegister;
public:
	static ssDriverBinder *Instance(void);
private:
	static void CleanUp(void);
protected:
	ssDriverBinder();
private:
	bool Register(const string &_major, ssDriverFactoryFunction _func);

	bool RegisterDescription(const ssDriverDescription *_desc);
	bool DumpDescriptionSub(const string &_fname);

public:
	ssSoundDriver *CreateDriver(const ssDriverConfig *_config) const;

	static bool DumpDescription(const string &_fname);

private:
	static ssDriverBinder *m_Instance;

	typedef multimap<string, ssDriverFactoryFunction> ssDriverMap;
	ssDriverMap m_Map;

	typedef map<string, const ssDriverDescription *> ssDescMapType;
	typedef map<string, ssDescMapType> ssDescMapName;
	ssDescMapName m_DescMap;
};

#endif // __SSDRIVERBINDER_H__
