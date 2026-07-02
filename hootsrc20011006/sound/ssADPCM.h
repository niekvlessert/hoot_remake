#ifndef __SSADPCM_H__
#define __SSADPCM_H__

#include "ssSoundChip.h"

class ssADPCM : public ssSoundChip
{
public:
	enum Mode {
		MODE_ADPCM = 0,
		MODE_8BITPCM,
		MODE_16BITPCM,
	};
	enum VolMode {
		MODE_OKI = 0,
		MODE_X68K_PCM8,
	};
public:
	ssADPCM(int _num, VolMode _vm = MODE_OKI);
	virtual ~ssADPCM();

public:
	void Update(SHORT **_buffer, DWORD _count);

	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	void SetMask(DWORD _mask);
	DWORD GetMask(void);

protected:
	void Initialize();

protected:
	void Play(int _ch, void *_adr, int _length);
	void Stop(int _ch);
	void SetVol(int _ch, int _vol);
	void SetFreq(int _ch, int _freq);
	void SetPan(int _ch, int _pan);
	void SetMode(int _ch, Mode _mode);

	void SetLSBFirst(bool _b);
	void SetNoiseReduction(bool _b);

	bool IsPlaying(int _ch) const;

private:
	static int index_shift[8];
	int diff_lookup[49*16];
	unsigned int volume_table[16];

	static BYTE release_data[];

	void compute_tables(VolMode _vm);

private:
	struct ADPCM
	{
		bool playing;
		bool release;

		BYTE *data;
		int sample;
		int signal;
		int prev_signal;
		int step;

		int length;
		int volume;


		int count;
		int incr;

		int voll;
		int volr;

		Mode mode;
	};

	int m_numvoice;
	ADPCM *m_voice;
	int m_rate;

	int m_bitorder;
	bool m_noise_reduction;

	void Fetch(int _ch);
};

#endif // __SSADPCM_H__
