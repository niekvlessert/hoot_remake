#ifndef __SS053260_H__
#define __SS053260_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

class ss053260 : public ssSoundChip
{
public:
public:
	ss053260(int _idTimer = 0);
	~ss053260();

	bool Initialize(int _clock, BYTE *_pcmdata, DWORD _pcmsize);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	void Reset(void);
	void EnableIRQ(bool _b);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

private:
	ssTimer *m_Timer;

	enum {
		MAX_PCM = 4,
	};
	BYTE m_reg[0x30];

	int m_sampling_rate;
	int m_clock;
	int m_mode;
	BYTE *m_pcmdata;
	DWORD m_pcmsize;

protected:
	ssTrackInfo info[MAX_PCM];

private:
	void InitDeltaTable( void );
	void check_bounds( int channel );

private:
	unsigned long delta_table[0x1000];

	struct K053260_channel_def {
		unsigned long		rate;
		unsigned long		size;
		unsigned long		start;
		unsigned long		bank;
		unsigned long		volume;
		int					play;
		unsigned long		pan;
		unsigned long		pos;
//#if INTERPOLATE_SAMPLES
		int					steps;
		int					stepcount;
//#endif
		int					loop;
		int					ppcm; /* packed PCM ( 4 bit signed ) */
		int					ppcm_data;
		int					reverse;
	} K053260_channel[4];
};

#endif // __SS053260_H__
