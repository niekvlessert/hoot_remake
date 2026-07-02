#ifndef __SSSTEREO2151_H__
#define __SSSTEREO2151_H__

#include "sound/ssYM2151.h"


class ssStereo2151 : public ssYM2151
{
public:
	ssStereo2151(int _idTimerA, int _idTimerB);
	virtual ~ssStereo2151();

	WORD GetBufferFlag(int _b) const;
	void SetVolume(int _b, int _p, short _vol);
	short GetVolume(int _b, int _p) const;
private:
	short m_Volume[2][2];
};

#endif // __SSSTEREO2151_H__
