#ifndef __SSMSM6295_H__
#define __SSMSM6295_H__

#include "ssSoundChip.h"
#include "ssADPCM.h"
#include "ssTrackInfo.h"

class ssMSM6295 : public ssADPCM
{
public:
	ssMSM6295();
	~ssMSM6295();

	bool Initialize(int _basefreq, BYTE *_pcmdata);

	void Update(SHORT **_buffer, DWORD _count);
	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	void SetBank(int _key, int _bank);
	void SetBankRegion(int _key1, int _key2, int _bank);

	void SetAdrMask(int _mask);

	// --------
	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

protected:
	ssTrackInfo info[4];

private:
	int m_basefreq;
	BYTE *m_pcmdata;

	enum {
		STATE_INI = 0,
		STATE_KEYON,
	};
	BYTE m_state;
	int m_key;
	int m_adr_mask;

	int m_bank[128];
};

#endif // __SSMSM6295_H__
