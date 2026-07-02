#ifndef __SSYM2608_H__
#define __SSYM2608_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

#include "sound/ssFMTimer.h"
#include "sound/ssAY8910.h"

class ssYM2608 : public ssSoundChip,
				 public ssFMTimer
{
public:
	static const DWORD MAX_YM2608;

	ssYM2608(int _idTimerA, int _idTimerB);
	virtual ~ssYM2608();

	bool Initialize(int _baseclock,
		void *_pcmrom, int _pcmsize);

	void TimerAOver(void);
	void TimerBOver(void);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;
	void SetVolume(int _b, int _p, short _vol);
	void SetVolumeDev(int _dev, short _vol);
	short GetVolume(int _b, int _p) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);
	void SetMask(DWORD _mask);
	DWORD GetMask(void);

protected:
	enum {
		INFO_ADPCM = 6,
		INFO_RHYTHM = 7,
	};
	int m_baseclock;
	ssTrackInfo info[6 + 1 + 6];

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

#endif // __SSYM2608_H__
