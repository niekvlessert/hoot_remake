#include "StdAfx.h"
#include "ssFile.h"


ssFile::ssFile()
{
	opened = false;
}

ssFile::ssFile(const string &_fname)
{
	Close();
	Open(_fname);
}

ssFile::~ssFile()
{
	Close();
}

bool ssFile::Open(const string &_fname)
{
	bool ret = false;

	Close();

	hFile = ::CreateFile(_fname.c_str(),
						 GENERIC_READ,
						 0,
						 NULL,
						 OPEN_EXISTING, // OPEN_ALWAYS
						 FILE_ATTRIBUTE_NORMAL,
						 NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		list<string>::const_iterator i;
		for (i = m_path.begin(); i != m_path.end(); i++) {
		string fname = *i + _fname;
			hFile = ::CreateFile(fname.c_str(),
								 GENERIC_READ,
								 0,
								 NULL,
								 OPEN_EXISTING, // OPEN_ALWAYS
								 FILE_ATTRIBUTE_NORMAL,
								 NULL);
			if (hFile != INVALID_HANDLE_VALUE) {
				break;
			}
		}
	}

	if (hFile != INVALID_HANDLE_VALUE) {
		opened = true;
		ret = true;
	}

	return ret;
}

bool ssFile::Close(void)
{
	bool ret = false;

	if (opened) {
		BOOL r = ::CloseHandle(hFile);
		if (r) {
			ret = true;
		}
		opened = false;
	}

	return ret;
}


int ssFile::GetSize(void) const
{
	int ret = -1;

	if (opened) {
		DWORD size = ::GetFileSize(hFile, NULL);
		if (size < 0x80000000) { // size = 0xffffffff if error
			ret = (int)size;
		}
	}

	return ret;
}

int ssFile::Read(void *_buff, int _size, int _offset)
{
	int ret = -1;

	if (!opened) return ret;

	if (_offset != -1) {
		Seek(_offset);
	}

	DWORD size;
	if (_size == -1) {
		size = GetSize();
	} else {
		size = _size;
	}

	DWORD rsize;

	BOOL r = ::ReadFile(hFile, _buff, size, &rsize, NULL);

	if (r) {
		ret = (int)rsize;
	}

	return ret;
}

int ssFile::Seek(int _point, DWORD _method)
{
	int ret = -1;

	if (!opened) return ret;

	DWORD point;
	point = ::SetFilePointer(hFile, _point, NULL, _method);

	if (point < 0x80000000) {
		ret = (int)point;
	}

	return ret;
}

void ssFile::AddSearchPath(const string &_path)
{
	m_path.push_back(_path);
}

void ssFile::AddSearchPath(const list<string> &_path)
{
	list<string>::const_iterator i;
	for (i = _path.begin(); i != _path.end(); i++) {
		string path = *i;
		m_path.push_back(path);
	}
}
