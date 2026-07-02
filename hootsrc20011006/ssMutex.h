#ifndef __SSMUTEX_H__
#define __SSMUTEX_H__

class ssMutex
{
public:
	ssMutex(LPCTSTR _name = NULL);
	virtual ~ssMutex();

	void Lock(void);
	void Unlock(void);
private:
	HANDLE m_hMutex;
};

inline ssMutex::ssMutex(LPCTSTR _name)
{
	m_hMutex = ::CreateMutex(NULL, FALSE, _name);
}

inline ssMutex::~ssMutex()
{
	Lock();
	::CloseHandle(m_hMutex);
}

inline void ssMutex::Lock(void)
{
	::WaitForSingleObject(m_hMutex, INFINITE);
}

inline void ssMutex::Unlock(void)
{
	::ReleaseMutex(m_hMutex);
}

#endif // __SSMUTEX_H__
