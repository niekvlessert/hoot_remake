#include "StdAfx.h"
#include <io.h>
#include "ssUnZip.h"

#if 0
#ifndef _DEBUG
#pragma comment(lib, "zlibstat.lib")
#else
#pragma comment(lib, "zlibstatd.lib")
#endif
#endif

const list<string> *ssUnZip::m_SearchPath = NULL;

ssUnZip::ssUnZip()
{
	m_archive = NULL;
}

ssUnZip::~ssUnZip()
{
	Close();
}

void ssUnZip::SetSearchPath(const list<string> *_path)
{
	m_SearchPath = _path;
}

bool ssUnZip::IsExist(const string &_fname)
{
	string ext(".zip");
	string filename;

	filename = _fname + ext;

	list<string>::const_iterator i;
	for (i = m_SearchPath->begin(); i != m_SearchPath->end(); i++) {
		string path = *i + filename;
		if (_access(path.c_str(), 4) == 0) {
			return true;
		}
	}

	return false;
}

bool ssUnZip::Open(const string &_fname)
{
	string ext(".zip");
	string filename;

	filename = _fname + ext;

	list<string>::const_iterator i;
	for (i = m_SearchPath->begin(); i != m_SearchPath->end(); i++) {
		string path = *i + filename;
		m_archive = unzOpen(path.c_str());
		if (m_archive != NULL) {
			return true;
		}
	}

	return false;
}

bool ssUnZip::Close()
{
	if (m_archive) {
		unzClose(m_archive);
		m_archive = NULL;
	}
	return true;
}

int ssUnZip::Size(const string &_fname)
{
	int ret;

	if (m_archive == NULL) {
		return -1;
	}

	ret = unzLocateFile(m_archive, _fname.c_str(), 0);
	if (ret != UNZ_OK) {
		return -1;
	}
	unz_file_info info;
	ret = unzGetCurrentFileInfo(m_archive,
		&info,
		NULL, 0,
		NULL, 0,
		NULL, 0);
	if (ret != UNZ_OK) {
		return -1;
	}

	return info.uncompressed_size;
}

int ssUnZip::Load(const string &_fname, void *_buff, int _size)
{
	int ret;

	if (m_archive == NULL) {
		return -1;
	}

	ret = unzLocateFile(m_archive, _fname.c_str(), 0);
	if (ret != UNZ_OK) {
		return -1;
	}

	if (_size == 0) {
		unz_file_info info;
		ret = unzGetCurrentFileInfo(m_archive,
									&info,
									NULL, 0,
									NULL, 0,
									NULL, 0);
		if (ret != UNZ_OK) {
			return -1;
		}
		_size = info.uncompressed_size;
	}

	ret = unzOpenCurrentFile(m_archive);
	if (ret != UNZ_OK) {
		return -1;
	}

	ret = unzReadCurrentFile(m_archive, _buff, _size);

	unzCloseCurrentFile(m_archive);

	return ret;
}
