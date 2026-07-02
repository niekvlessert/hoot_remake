#ifndef __SS007232_H__
#define __SS007232_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ss007232 : public ssSoundChip
{
public:
	static const DWORD MAX_007232;

	ss007232();
	virtual ~ss007232();

	bool Initialize(int _baseclock, BYTE *pcmdata);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	void SetBank(int _ch, int _bank);
	void SetVol(int _ch, int _p, int _vol);
	void SetVol_stereo(int _ch, int _vol);

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
	static const int ADDR_SHIFT;

	BYTE *m_pcmdata;
	BYTE m_reg[16];
	int m_stepdata[512];
	BYTE m_kctable[512];
	struct {
		short vol[2];
		BYTE *start;
		int offset;
		int incr;
		int bank;
		bool play;
		bool loop;
	} m_ch[2];
};

#endif // __SS007232_H__
