#ifndef __SSC140_H__
#define __SSC140_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssC140 : public ssSoundChip
{
public:
	enum {
		TYPE_SYSTEM2 = 0,
		TYPE_SYSTEM21,
		TYPE_SYSTEM21_B,
	};
	ssC140(int _type = TYPE_SYSTEM2);
	~ssC140();

	bool Initialize(int _basefreq, BYTE *_pcmdata);

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
		MAX_VOICE = 24,
		MAX_PCM = MAX_VOICE,
	};

protected:
	ssTrackInfo info[MAX_VOICE];

	/* from c140.{c,h} (MAME) */
private:
	int m_type;
	int sample_rate;
	int baserate;
	void *pRom;
	BYTE REG[0x200];

	SHORT pcmtbl[8];

	struct VOICE
	{
		long	ptoffset;
		long	pos;
		long	key;
		//--work
		long	lastdt;
		long	prevdt;
		long	dltdt;
		//--reg
		long	rvol;
		long	lvol;
		long	frequency;
		long	bank;
		long	mode;

		long	sample_start;
		long	sample_end;
		long	sample_loop;
	};
	VOICE voi[MAX_VOICE];


	void init_voice( VOICE *v );
	long find_sample( long adrs, long bank);
};

#endif // __SSC140_H__
