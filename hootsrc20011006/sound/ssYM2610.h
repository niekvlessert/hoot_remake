#ifndef __SSYM2610_H__
#define __SSYM2610_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

#include "sound/ssFMTimer.h"
#include "sound/ssAY8910.h"

class ssYM2610 : public ssSoundChip,
				 public ssFMTimer
{
public:
	static const DWORD MAX_YM2610;

	ssYM2610(int _idTimerA, int _idTimerB);
	virtual ~ssYM2610();

	bool Initialize(int _baseclock,
		void *_pcmroma, int _pcmsizea,
		void *_pcmromb, int _pcmsizeb);

	void TimerAOver(void);
	void TimerBOver(void);

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
	enum {
		INFO_ADPCMA = 4,
		INFO_ADPCMB = 10,
	};
	int m_baseclock;
	ssTrackInfo info[4 + 6 + 1];

private:
	static DWORD m_bitmap;

	int m_ChipNo;
	ssAY8910 *m_SSG;

	void calc_notetable(int _pris, int _ssg_pris);
	void write_reg(BYTE _reg, BYTE _data, BYTE _c);
	BYTE m_reg1;
	BYTE m_reg2;
	BYTE m_pris;
	BYTE reg[256*2];
	BYTE notetable[2048 * 8];
};

#endif // __SSYM2610_H__
