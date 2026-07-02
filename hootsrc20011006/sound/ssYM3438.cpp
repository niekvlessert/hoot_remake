#include "StdAfx.h"
#include "sound/ssYM3438.h"
#include "sound/ssAY8910.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"
#include "ssUnZip.h"

// インチキ YM-3438 エンジン(実際は YM-2608)

ssYM3438::ssYM3438(int _idTimerA, int _idTimerB)
	: ssYM2608(_idTimerA, _idTimerB)
{
}

ssYM3438::~ssYM3438()
{
}

bool ssYM3438::Initialize(int _baseclock)
{
	bool ret = ssYM2608::Initialize(_baseclock, 0, 0);
	int t;
	for (t = 0; t < 6; t++) {
		InitTrackInfo(&info[t]);
		sprintf(info[t].name, "YM-3438 FM#%d", t);
	}

	return ret;
}


int ssYM3438::GetTrackCount(void) const
{
	return 6;
}
