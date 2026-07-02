#ifndef __SSUNZIP_H__
#define __SSUNZIP_H__

#include <unzip.h>

class ssUnZip
{
public:
	ssUnZip();
	~ssUnZip();

	static void SetSearchPath(const list<string> *_path);
	static bool IsExist(const string &_fname);

	bool Open(const string &_fname);
	bool Close();
	int Size(const string &_fname);
	int Load(const string &_fname, void *_buff, int _size);
private:
	static const list<string> *ssUnZip::m_SearchPath;

	unzFile m_archive;
};

#endif __SSUNZIP_H__
