#ifndef __SSSEGAPCM2_H__
#define __SSSEGAPCM2_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssSEGAPCM2 : public ssSoundChip
{
public:
	enum {
		TYPE_STEREO = 0,
		TYPE_MONO,
	};
private:
	enum {
		MAX_PCM = 28,
		INCR_SHIFT = 14,
		ENV_SHIFT = 20,
		ENV_SHIFT2 = 12,
	};
public:
	ssSEGAPCM2(int _type = TYPE_STEREO);
	~ssSEGAPCM2();

	bool Initialize(int _rate, BYTE *_pcmdata, int _banksize = 0x100000);

	void SetChannel(BYTE _data);
	void SetReg(BYTE _data);
	void SetData(BYTE _data);
	void SetBank(BYTE _data);

	BYTE ReadStatus(void);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info[MAX_PCM];

private:
	static const int m_chtbl[];
	static const int pantbl_stereo[];
	static const int pantbl_mono[];

private:
	enum {
		ENV_NONE = 0,
		ENV_ATTACK,
		ENV_DECAY,
		ENV_RELEASE,
	};

private:
	int m_type;

	BYTE m_regs[16 * MAX_PCM];

	BYTE *m_pcmdata;
	int m_banksize;

	BYTE m_ch;
	BYTE m_reg;
	BYTE m_bankL;
	BYTE m_bankR;

	double m_pitchtbl[0x1000];
	int m_voltbl[128];
	int m_pantbl[16];
	int m_infopantbl[16];

	double m_ar_table[16];
	double m_dr_table[16];
	double m_rr_table[16];

	struct voice {
		int play;
		int start;
		int size;
		int incr;
		int pos;
		int volume;
		int pan;
		int loop;

		int attackrate;
		int attacklevel;
		int decayrate;
		int sustainlevel;
		int releaserate;

		int envphase;
		int envincr;
		int envlevel;
		int envgoal;
	} m_voi[MAX_PCM];
};

#endif // __SSSEGAPCM2_H__
