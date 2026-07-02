#ifndef __SSMONO2151_H__
#define __SSMONO2151_H__

#include "sound/ssYM2151.h"


class ssMono2151 : public ssYM2151
{
public:
	ssMono2151(int _idTimerA, int _idTimerB);
	virtual ~ssMono2151();

	void Write(int _adr, BYTE _data);
private:
	BYTE m_reg;
};

#endif // __SSMONO2151_H__
