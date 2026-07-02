#ifndef __SSRCONFIGLOADER_H__
#define __SSRCONFIGLOADER_H__

#include "ssIfFolder.h"

class ssConfigLoader
{
public:
	static bool Load(const string &_fname, vector<ssIfFolder *> &_configlist);
	static bool Free(vector<ssIfFolder *> &_configlist);
	static bool Dump(vector<ssIfFolder *> &_configlist);
};

#endif // __SSCONFIGLOADER_H__
