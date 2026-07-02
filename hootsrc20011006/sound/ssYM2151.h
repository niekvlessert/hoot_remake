#ifndef __SSYM2151_H__
#define __SSYM2151_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

#include "sound/ssFMTimer.h"

class ssYM2151 : public ssSoundChip,
				 public ssFMTimer
{
public:
	static const DWORD MAX_YM2151;

	ssYM2151(int _idTimerA, int _idTimerB);
	virtual ~ssYM2151();

	bool Initialize(int _baseclock);

	void TimerAOver(void);
	void TimerBOver(void);

	virtual void Write(int _adr, BYTE _data);
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
	ssTrackInfo info[8];

private:
	static DWORD m_bitmap;

	int m_ChipNo;

	BYTE m_reg;
	BYTE reg[256];
};

#endif // __SSYM2151_H__
