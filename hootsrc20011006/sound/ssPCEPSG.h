#ifndef __SSPCEPSG_H__
#define __SSPCEPSG_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssPCEPSG : public ssSoundChip
{
private:
	enum {
		MAX_CH = 6,
		INCR_SHIFT = 16,
	};
public:
	ssPCEPSG();
	~ssPCEPSG();

	bool Initialize(int _clock);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info[MAX_CH];

private:
	BYTE m_reg0[10];
	BYTE m_reg[0x10 * MAX_CH + 0x20 * MAX_CH];
	int m_voltbl[32];
	BYTE m_kctable[0x1000];

	int m_sampling_rate;
	int m_freqbase;

	struct channel
	{
		int freq;
		int vol;
		int voll;
		int volr;
		char wave[32];
		int wave_ptr;

		int mode;

		int offset;

		int noise;
		int noise_freq;
		BYTE noise_state;
		int noise_counter;
		int noise_seed;
	};

	int m_ch_sel;
	int m_voll;
	int m_volr;

	BYTE m_lfoctl;
	BYTE m_lfofreq;
	int m_lfofreqtbl[32 * 4];
	int m_lfooffset;

	struct channel m_ch[MAX_CH];
};

#endif // __SSPCEPSG_H__
