#ifndef __SSSEGAPCM_H__
#define __SSSEGAPCM_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssSEGAPCM : public ssSoundChip
{
public:
	ssSEGAPCM();
	~ssSEGAPCM();

	bool Initialize(int _basefreq, int _banksize, int _bankmask, BYTE *_pcmdata);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info[16];

private:
	enum {
		MAX_PCM = 16,
		INCR_SHIFT = 16,
		R_LVOL = 0x02,
		R_RVOL = 0x03,
		R_PITCH = 0x07,
	};
	BYTE m_reg[256];

	int m_sampling_rate;
	int m_basefreq;
	int m_banksize;
	int m_bankmask;
	BYTE *m_pcmdata;

	bool m_keyon[MAX_PCM];
	WORD m_start[MAX_PCM];
	WORD m_end[MAX_PCM];
	int m_bank[MAX_PCM];
	int m_offset[MAX_PCM];
	int m_incr[MAX_PCM];
};

#endif // __SSSEGAPCM_H__
