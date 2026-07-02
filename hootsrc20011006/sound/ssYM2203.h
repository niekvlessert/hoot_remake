#ifndef __SSYM2203_H__
#define __SSYM2203_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

#include "sound/ssFMTimer.h"
#include "sound/ssAY8910.h"

class ssYM2203 : public ssSoundChip,
				 public ssFMTimer
{
public:
	static const DWORD MAX_YM2203;

	ssYM2203(int _idTimerA, int _idTimerB);
	virtual ~ssYM2203();

	bool Initialize(int _baseclock);

	void TimerHandler(int _c, int _cnt, double _step);
	void IRQHandler(int _irq);

	void TimerAOver(void);
	void TimerBOver(void);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;
	void SetVolume(int _b, int _p, short _vol);
	short GetVolume(int _b, int _p) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);
	void SetMask(DWORD _mask);
	DWORD GetMask(void);

protected:
	int m_baseclock;
	ssTrackInfo info[3];

private:
	static DWORD m_bitmap;

	int m_ChipNo;
	ssAY8910 *m_SSG;

	void calc_notetable(int _pris, int _ssg_pris);
	BYTE m_reg;
	BYTE m_pris;
	BYTE reg[256];
	BYTE notetable[2048 * 8];
};

#endif // __SSYM2203_H__
