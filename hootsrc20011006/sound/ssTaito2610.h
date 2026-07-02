#ifndef __SSTAITO2610_H__
#define __SSTAITO2610_H__

#include "sound/ssYM2610.h"

class ssTaito2610 : public ssYM2610
{
public:
	ssTaito2610(int _idTimerA, int _idTimerB)
		: ssYM2610(_idTimerA, _idTimerB) {
	}
	WORD GetBufferFlag(int _b) const;
	void SetVolume(int _b, int _p, short _vol);
	short GetVolume(int _b, int _p) const;
private:
	short m_Volume[2][2];
};

#endif // __SSTAITO2610_H__
