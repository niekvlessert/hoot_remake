//
// YM3812 defineitions
//
#ifndef __SSYM3812_H__
#define __SSYM3812_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

extern "C" {
#include "mame/sound/driver.h"
#include "mame/sound/fmopl.h"
}

class ssYM3812 : public ssSoundChip
{
public:
	static const DWORD MAX_YM3812;

	ssYM3812(int _idTimerA, int _idTimerB);
	virtual ~ssYM3812();

	bool Initialize(int _baseclock);

	void TimerHandler(int _c, double _step);
	void IRQHandler(int _irq);

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
	int m_baseclock;
	ssTrackInfo info[9];

private:
	static DWORD m_bitmap;

	int m_ChipNo;
	ssTimer *m_TimerA;
	ssTimer *m_TimerB;

	FM_OPL *F3812;

	void calc_notetable(void);

	BYTE m_reg;
	BYTE reg[256];
	BYTE notetable[2048 * 8];
};

#endif // __SSYM3812_H__
