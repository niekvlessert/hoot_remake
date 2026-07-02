#ifndef __SSYAY8910_H__
#define __SSYAY8910_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"

class ssAY8910 : public ssSoundChip
{
public:
	enum {
		TYPE_NORMAL = 0,
		TYPE_YM2203,
		TYPE_YM2608,
		TYPE_YM2610,
		TYPE_M88 = 0x10,
		TYPE_YM2203_M88,
		TYPE_YM2608_M88,
		TYPE_YM2610_M88,
	};
	enum {
		PORT_A = 0,
		PORT_B,
	};
public:
	static const DWORD MAX_AY8910;

	ssAY8910(int _type = TYPE_NORMAL);
	~ssAY8910();
	bool Initialize(int _baseclock);

	void Write(int _adr, BYTE _data);
	void WriteInfo(int _adr, BYTE _data);
	BYTE Read(void);

	void SetClock(int _baseclock);

	void SetPortWriteHandler(int _port, void (*_handler)(BYTE _data));
	void SetPortReadHandler(int _port, BYTE (*_handler)(void));

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);
	void SetMask(DWORD _mask);
	DWORD GetMask(void);

protected:
	ssTrackInfo info[3];

private:
	static DWORD m_bitmap;

	int m_type;

	int m_ChipNo;

	int m_baseclock;
	BYTE m_kctable[0x1000];

	BYTE m_reg;
	BYTE reg[16];

	void (*PAwrite)(BYTE _data);
	BYTE (*PAread)(void);
	void (*PBwrite)(BYTE _data);
	BYTE (*PBread)(void);
};

#endif // __SSAY8910_H__
