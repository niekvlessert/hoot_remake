#ifndef __SSDRIVERREGISTER_H__
#define __SSDRIVERREGISTER_H__

#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverBinder.h"

class ssDriverRegister
{
public:
	ssDriverRegister(string &_major,
					 ssDriverFactoryFunction _func,
					 const ssDriverDescription *_desc = NULL);
};

#endif // __SSDRIVERREGISTER_H__
