// KONAMI SCC (051649) definition
#ifndef __SS051649_H__
#define __SS051649_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

#define FREQBASEBITS	16


struct k051649_interface
{
	int master_clock;	/* master clock */
	int volume;			/* playback volume */
};

class ss051649 : public ssSoundChip
{
public:
	ss051649();
	~ss051649();

	bool Initialize(int clk);

	void Write(int _adr, BYTE _data);
	BYTE Read(WORD _adr);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

	void InitSccWork();

	void SetSccEnable();						// SCC アクセス有効にする
	void SetSccDisable();						// SCC アクセス禁止にする
	int GetSccAccessStatus();					// SCC アクセス可否問い合わせ

private:
	enum {
		MAX_VOICE = 5,
		MAX_PCM = MAX_VOICE,
	};

protected:
	ssTrackInfo info[MAX_VOICE];

	/* from k051649.{c,h} (MAME) */
private:
	int sample_rate;
	int mclock;
	int rate;
	BYTE REG[0x200];
	BYTE m_kctbl[0x1000];
	int enable;					// SCC アクセスフラグ(1:enable, 0:disable)

	/* this structure defines the parameters for a channel */
	struct k051649_sound_channel
	{
		ULONG counter;
		int frequency;
		int volume;
		int key;
		/*unsigned char waveform[32];*/
		signed char waveform[32];		/* 19991207.CAB */
	};
	k051649_sound_channel channel_list[MAX_VOICE];

	/* mixer tables and internal buffers */
	SHORT *mixer_table;
	SHORT *mixer_lookup;
	SHORT *mixer_buffer;

	void make_mixer_table(int voices);
};

#endif // __SS051649_H__
