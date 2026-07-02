#ifndef __ssMSM6258V_H__
#define __ssMSM6258V_H__

#include "ssSoundChip.h"
#include "ssADPCM.h"
#include "ssTrackInfo.h"

class ssMSM6258V : public ssADPCM
{
public:
	ssMSM6258V();
	~ssMSM6258V();

	bool Initialize(int _basefreq);

	void SetAdpcmSize(int _size);
	void SetAdpcmAddr(void *_adr);
	void SetStat(BYTE _data);
	BYTE Read(int _adr);

	BYTE GetPanAndRate(void);
	void SetPanAndRate(BYTE _value);

	// --------
	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	void Update(SHORT **_buffer, DWORD _count);

protected:
	ssTrackInfo info;

private:
	int m_basefreq;
	void *m_addr;
	int m_size;

	BYTE m_ppi;
};

#endif // __ssMSM6258V_H__
