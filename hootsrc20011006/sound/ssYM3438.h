#ifndef __SSYM3438_H__
#define __SSYM3438_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

#include "sound/ssYM2608.h"

class ssYM3438 : public ssYM2608
{
public:
	ssYM3438(int _idTimerA, int _idTimerB);
	virtual ~ssYM3438();

	bool Initialize(int _baseclock);

	int GetTrackCount(void) const;
};

#endif // __SSYM3438_H__
