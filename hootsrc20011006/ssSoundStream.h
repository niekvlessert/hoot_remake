#ifndef __SSSOUNDSTREAM_H__
#define __SSSOUNDSTREAM_H__

#include "ssSoundChip.h"

class ssSoundStream
{
	friend class ssSoundDriverManager;
public:
	enum {
		MAX_BUFF = 8, // 1 チップ最大 8 ストリームまで
	};

	// ストリームの種類
	enum {
		F_STEREO               = 0x0001, // ステレオ [L,R,L,R,...    ] (バッファの長さ2倍)
		F_MONO                 = 0x0002, // モノラル [C,C,...        ]
		F_LEFT                 = 0x0004, // 左のみ   [L,L,...        ]
		F_RIGHT                = 0x0008, // 右のみ   [R,R,...        ]
		F_SEPARATE             = 0x0010, // 左右分離 [LR,LR,...      ]
		F_STEREO_LONG          = 0x0020, // 32ビット [lL,lR,lL,lR,...] (バッファの長さ4倍)
		F_STEREO_SEPARATE_LONG = 0x0040, // 32ビット [lLR,lLR,...    ] (バッファの長さ2倍)
	};
protected:
	ssSoundStream(ssSoundChip *_chip);
	~ssSoundStream();

private:
	ssSoundChip *chip;
	int num_buff;
	WORD flag[MAX_BUFF];
	short *buff[MAX_BUFF];
};

#endif
