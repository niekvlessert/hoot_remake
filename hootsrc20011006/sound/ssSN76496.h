//
// SN76496 definition
//
#ifndef __SN76496_H__
#define __SN76496_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssSN76496 : public ssSoundChip
{
public:


	ssSN76496();
	virtual ~ssSN76496();

	bool Initialize(int _clock);

	void Write(BYTE _data);

	void Update(SHORT **_buffer, DWORD _count);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

protected:
	ssTrackInfo info[4]; // element 3(noise) is dummy.

private:
	struct SN76496 {
		int Channel;
		int SampleRate;
		unsigned int UpdateStep;
		int VolTable[16];		/* volume table         */
		int Register[8];		/* registers */
		int LastRegister;		/* last register written */
		int Volume[4];			/* volume of voice 0-2 and noise */
		unsigned int RNG;		/* noise generator      */
		int NoiseFB;			/* noise feedback mask */
		int Period[4];
		int Count[4];
		int Output[4];
	};

	SN76496 sn;

	void init_sub(int clock, int sample_rate);
	void set_gain(int gain);
	void set_clock(int clock);

private:
	int sample_rate;

	BYTE m_kctable[0x1000];
	BYTE m_voltbl[16];
};

#endif //__SN76496_H__
