#ifndef __SSYM2413_H__
#define __SSYM2413_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

#include "emu2413/emu2413.h"


class ssYM2413 : public ssSoundChip
{
public:
	ssYM2413();
	virtual ~ssYM2413();

	bool Initialize(int _baseclock);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);
	void SetMask(DWORD _mask);
	DWORD GetMask(void);

protected:
	ssTrackInfo info[9+5];

private:
	void UpdateMask(void);

	OPLL *m_opll;

	BYTE m_reg;
	BYTE reg[256];
	BYTE notetable[512*8];

	BYTE m_reg0e;
};

#endif // __SSYM2413_H__
