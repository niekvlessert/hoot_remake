#ifndef __SSMEMORYDUMP_H__
#define __SSMEMORYDUMP_H__

#include "ssSoundChip.h"

class ssMemoryDump : public ssSoundChip
{
public:
	ssMemoryDump();
	~ssMemoryDump();

	bool SetAddress(BYTE *_address, DWORD _size = 0x300);

	void SetBYTE(int _offset, BYTE _data);
	void SetWORD(int _offset, WORD _data);
	void SetDWORD(int _offset, DWORD _data);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);
	void SetMask(DWORD _mask);
	DWORD GetMask(void);

private:
	BYTE *m_address;
	DWORD m_size;
	BYTE m_buff[0x300];
};

#endif // __SSMEMORYDUMP_H__
