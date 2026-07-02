#ifndef __SSFILE_H__
#define __SSFILE_H__

class ssFile
{
public:
	ssFile();
	ssFile(const string &_fname);
	~ssFile();

	bool Open(const string &_fname);
	bool Close(void);

	int GetSize(void) const;
	int Read(void *_buff, int _size = -1, int _offset = -1);
	int Seek(int _point, DWORD _method = FILE_BEGIN);

	void AddSearchPath(const string &_path);
	void AddSearchPath(const list<string> &_path);

private:
	bool opened;
	HANDLE hFile;

	list<string> m_path;
};

#endif // __SSFILE_H__
