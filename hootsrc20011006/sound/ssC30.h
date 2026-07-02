#ifndef __SSC30_H__
#define __SSC30_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssC30 : public ssSoundChip
{
public:
	enum {
		TYPE_STEREO = 0,
		TYPE_MONO,
		TYPE_WSG,
	};
public:
	ssC30(int _type = TYPE_STEREO);
	~ssC30();

	bool Initialize(int _basefreq, BYTE *_pcmdata);

	void Write(int _adr, BYTE _data);
	void WriteMAPPY(int _adr, BYTE _data);
	BYTE Read(int _adr);

	void Enable(bool _enable);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info[8];

private:
	int m_type;

	enum {
		MAX_PCM = 8,
		INCR_SHIFT = 16,
	};
	BYTE m_reg[0x40];

	int m_sampling_rate;
	int m_basefreq;
	bool m_stereo;
	BYTE *m_pcmdata;

	struct channel
	{
		int freq;
		int voll;
		int volr;
		BYTE *wave;
		int offset;
		int incr;

		BYTE noise;
		BYTE noise_state;
		int noise_counter;
		int noise_seed;
	};

	bool m_enable;
	struct channel m_ch[MAX_PCM];
};

#endif // __SSC30_H__
