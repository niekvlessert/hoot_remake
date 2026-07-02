#ifndef __SS054539_H__
#define __SS054539_H__

#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssTimer.h"

typedef void (*ss054539PanHandler)(int _trk, double _l, double _r, void *_param);

class ss054539 : public ssSoundChip
{
public:
	ss054539(int _timer = 0);
	~ss054539();

	bool Initialize(int _clock, BYTE *_pcmdata, DWORD _pcmsize);
	void SetPanHandler(ss054539PanHandler _handler, void *_param = NULL);

	void Write(int _adr, BYTE _data);
	BYTE Read(int _adr);

	void Reset(void);
	void EnableIRQ(bool _b);

	// --------
	int GetBufferCount(void) const;
	WORD GetBufferFlag(int _b) const;

	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track);

	int GetRegs(BYTE *_buffer, int _count, int _offset);

	void Update(SHORT **_buffer, DWORD _count);

private:
	enum {
		MAX_PCM = 8,
	};

	ssTimer *m_timer;
	ss054539PanHandler m_handler;
	void *m_handler_param;

protected:
	ssTrackInfo info[MAX_PCM];

private:
	int regupdate(void);
	void keyon(int channel);
	void keyoff(int channel);

private:
	double freq_ratio;
	double voltab[256];
	double pantab[0xf];

	unsigned char regs[0x230];
	unsigned char ram[0x4000];
	int cur_ptr;
	int cur_limit;
	unsigned char *cur_zone;
	unsigned char *rom;
	UINT32 rom_size;
	UINT32 mrom_mask;

	struct K054539_channel {
		UINT32 pos;
		UINT32 pfrac;
		INT32 val;
		INT32 pval;
	} channels[8];
};

#endif // __SS054539_H__
