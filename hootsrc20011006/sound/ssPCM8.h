#ifndef __SSPCM8_H__
#define __SSPCM8_H__

#include "ssSoundChip.h"
#include "ssADPCM.h"
#include "ssTrackInfo.h"

class ssPCM8 : public ssADPCM
{
public:
	ssPCM8();
	~ssPCM8();

	bool Initialize(void);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

	// --------
	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

public:
	void Play(int _ch, void *_adr, int _length);
	void Play(int _ch, void *_adr, int _offset, int _length);
	void Stop(int _ch);
	void SetVol(int _ch, int _vol);
	void SetFreq(int _ch, int _freq);
	void SetPan(int _ch, int _pan);
	void SetMode(int _ch, ssADPCM::Mode _mode);

protected:
	ssTrackInfo info[8];

private:
	BYTE m_reg[16 * 8];
};

#endif // __SSPCM8_H__
