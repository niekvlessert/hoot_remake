#ifndef __SSTRACKINFO_H__
#define __SSTRACKINFO_H__

#include <math.h>

struct ssTrackInfo
{
	enum {
		NAME_MAX = 32,	// チャンネル名の最大長

		OCT4_A = 57,	// オクターブ 4 の A のキーコード

		PAN_OFF = 0,	// 左右 OFF
		PAN_LEFT = 1,	// 最も左
		PAN_CENTER = 4,	// 中央
		PAN_RIGHT = 7,	// 最も右
		PAN_MASK = 8,	// マスク
	};
	BYTE keyon;			// 0xff:ON 0x00:OFF other:その他
	BYTE keycode;		// 00= o0c
	BYTE volume;		// 0x00-0x7f
	BYTE pan;			// 0:off 1:left 4:center 7:right
	char name[NAME_MAX];
};

static inline int Hz2Kc(double _freq)
{
	int kco = (int)(12.0 * log(_freq/440.0) / log(2.0) + ssTrackInfo::OCT4_A + 0.5);
	if (kco < 0) {
		kco = 0;
	} else if (kco > 127) {
		kco = 127;
	}
	return kco;
}

static inline void InitTrackInfo(ssTrackInfo *_info)
{
	_info->keycode = ssTrackInfo::OCT4_A;
	_info->keyon = 0x00;
	_info->volume = 0;
	_info->pan = ssTrackInfo::PAN_CENTER;
	_info->name[0] = '\0';
}

#endif // __SSTRACKINFO_H__
