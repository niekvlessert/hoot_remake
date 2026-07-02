#ifndef __SS005289_H__
#define __SS005289_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

#define FREQBASEBITS	16

struct k005289_interface
{
	int master_clock;	/* master clock */
	int volume;			/* playback volume */
};

class ss005289 : public ssSoundChip
{
public:
	ss005289();
	~ss005289();

	bool Initialize(int _baseclock, BYTE *_wave0, BYTE *_wave1);

	void WriteCtrl(int _ch, BYTE _data);
	void WritePitch(int _ch, WORD _data);
	void WriteLatch(int _ch, BYTE _data);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info[2];

private:
	int m_sample_rate;
	int m_baseclock;

	BYTE *m_wave[2];

	BYTE m_kctable[0x1000];

	struct k005289_sound_channel {
		int frequency;
		int counter;
		int volume;
		const unsigned char *wave;

		int latch;
	};

	k005289_sound_channel m_ch[2];
};

#endif // __SS005289_H__
