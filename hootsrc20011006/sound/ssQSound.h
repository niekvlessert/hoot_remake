#ifndef __SSQSOUND_H__
#define __SSQSOUND_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssQSound : public ssSoundChip
{
public:
	ssQSound();
	~ssQSound();

	bool Initialize(int _clock, BYTE *_pcmdata);

	void WriteCommand(BYTE _data);
	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

private:
	enum {
		LENGTH_DIV = 1,
		QSOUND_CLOCKDIV = 166,
		QSOUND_CHANNELS = 16,
		MAX_VOICE = QSOUND_CHANNELS,
	};

private:
	typedef int QSOUND_SAMPLE;
	typedef signed char QSOUND_SRC_SAMPLE;
	struct QSOUND_CHANNEL
	{
		int bank;	   /* bank (x16)	*/
		int address;	/* start address */
		int pitch;	  /* pitch */
		int reg3;	   /* unknown (always 0x8000) */
		int loop;	   /* loop address */
		int end;		/* end address */
		int vol;		/* master volume */
		int pan;		/* Pan value */
		int reg9;	   /* unknown */

		/* Work variables */
		int key;		/* Key on / key off */

		int lvol;	   /* left volume */
		int rvol;	   /* right volume */
		int lastdt;	 /* last sample value */
		int offset;	 /* current offset counter */
	};

protected:
	ssTrackInfo info[MAX_VOICE];

private:
	BYTE m_reg[QSOUND_CHANNELS * 32];
	int sample_rate;
	int qsound_data;
	QSOUND_SRC_SAMPLE *qsound_sample_rom;

	struct QSOUND_CHANNEL qsound_channel[QSOUND_CHANNELS];
	int qsound_pan_table[33];
	float qsound_frq_ratio;
};

#endif // __SSQSOUND_H__
