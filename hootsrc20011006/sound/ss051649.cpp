/***************************************************************************

	Konami 051649 - SCC1 sound as used in Haunted Castle, City Bomber

	This file is pieced together by Bryan McPhail from a combination of
	Namco Sound, Amuse by Cab, Haunted Castle schematics and whoever first
	figured out SCC!

	The 051649 is a 5 channel sound generator, each channel gets it's
	waveform from RAM (32 bytes per waveform, 8 bit signed data).

	Frequency information for each channel appears to be 10 bits.

***************************************************************************/

// MAME からのパクり :-)
#include "StdAfx.h"
#include "sound/ss051649.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"


//----------------------------------------------------------------------------
//
// コンストラクタ、デストラクタ
//
ss051649::ss051649()
{
	mixer_table = new SHORT[512 * MAX_PCM * sizeof(SHORT)];
	mixer_lookup = NULL;
	mixer_buffer = NULL;
}

ss051649::~ss051649()
{
	delete [] mixer_table;
	mixer_table = NULL;
	mixer_lookup = NULL;
	mixer_buffer = NULL;

//	TRACE("SCCShutDown()\n");
}

//----------------------------------------------------------------------------
//
// 合成バッファ初期化
//
/* build a table to divide by the number of voices */
void ss051649::make_mixer_table(int voices)
{
	int count = voices * 256;
	int i;
	int gain = 8;

	/* find the middle of the table */
	mixer_lookup = mixer_table + (256 * voices);

	/* fill in the table - 16 bit case */
	for (i = 0; i < count; i++)
	{
		int val = i * gain * 16 / voices;
		if (val > 32767) val = 32767;
		mixer_lookup[ i] = val;
		mixer_lookup[-i] = -val;
	}
}

//
// ＳＣＣワーク初期化
//
void ss051649::InitSccWork()
{
	k051649_sound_channel *voice = channel_list;

	/* reset all the voices */
	for (int i = 0; i < MAX_PCM; i++) {
		voice[i].frequency = 0;
		voice[i].volume = 0;
		voice[i].counter = 0;
		voice[i].key = 0x00;			// key off
	}

	// ＳＣＣレジスタの初期化
	memset(REG, 0, sizeof(REG));

	// SCC アクセス無効
	enable = -1;
}

//
// SCC アクセス許可
//
void ss051649::SetSccEnable()
{
	enable = 1;
}

//
// SCC アクセス禁止
//
void ss051649::SetSccDisable()
{
	enable = 0;
}

//
// SCC アクセス可否問い合わせ
//
int ss051649::GetSccAccessStatus()
{
	return enable;
}


//----------------------------------------------------------------------------
//
// 初期化
//
bool ss051649::Initialize(int clk)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	sample_rate = config.sampling_rate;

	memset(REG, 0, sizeof(REG));

	/* get stream channels */
	mclock = clk;
	rate = sample_rate;

	/* build the mixer table */
	make_mixer_table(MAX_PCM);

	// ＳＣＣワーク初期化
	InitSccWork();

	// 鍵盤表示用キーコードテーブル生成
	m_kctbl[0] = 127;
	for (int i = 1; i < 0x1000; i++) {
		m_kctbl[i] = Hz2Kc((double)clk / ((double)i * 8.0));
	}

	for (i = 0; i < MAX_PCM; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "SCC #%d", i);
	}

	SetMask(0);

	return true;
}


//-----------------------------------------------------------------------------
//
// サウンドデータの合成
//
/* generate sound to the mix buffer */
void ss051649::Update(SHORT **_buffer, DWORD _count)
{
	int length = _count;
	short *mixer_buffer = (short *)_buffer[0];
	k051649_sound_channel *voice = channel_list;
	short *mix, *remix;
	int i,v,f,j,k;

	/* zap the contents of the mixer buffer */
	memset(mixer_buffer, 0, length * sizeof(short));

	for (j = 0; j < MAX_PCM; j++) {
		v = voice[j].volume;
		f = voice[j].frequency;
		k = voice[j].key;
		if (v && f && k) {
			const signed char *w = voice[j].waveform;			/* 19991207.CAB */
			int c = voice[j].counter;

			mix = mixer_buffer;

			/* add our contribution */
			if (m_mask & (1<<j)) {
				// mask
				for (i = 0; i < length; i++) {
					c+=(long)((((float)mclock / (float)((f+1) * 16))*(float)(1<<FREQBASEBITS)) / (float)(rate / 32));
				}
			} else {
				for (i = 0; i < length; i++) {
					int offs;

					/* Amuse source:  Cab suggests this method gives greater resolution */
					/* Sean Young 20010417: the formula is really: f = clock/(16*(f+1))*/
					c+=(long)((((float)mclock / (float)((f+1) * 16))*(float)(1<<FREQBASEBITS)) / (float)(rate / 32));
					offs = (c >> 16) & 0x1f;
					*mix++ += (w[offs] * v)>>3;						/* 19991207.CAB */
				}
			}

			/* update the counter for this voice */
			voice[j].counter = c;
		}
	}

	/* mix it down */
	mix = mixer_buffer;
	remix = mix;
	for (i = 0; i < length; i++) {
		*remix++ = mixer_lookup[*mix++];
	}
}


/********************************************************************************/

void ss051649::Write(int _adr, BYTE _data)
{
	BYTE offset = _adr & 0xff;
	const BYTE data = _data;
#if 0
	// 線形のほうがレベルメータがきれいなので…。
	const BYTE infovol[] = {  0,   5,  10,  19,
							 28,  37,  46,  55,
							 64,  73,  82,  91,
							100, 109, 118, 127};
#endif


	// 9890h-989fh の場合の前処理
	if (offset >= 0x90 && offset < 0xa0) {
		offset -= 0x10;
	}

	REG[offset] = data;

//	TRACE("SCC reg:%02x data:%02x\n", offset, data);

	// 0x00～0x7f : wave data
	if (offset < 0x80) {
		const BYTE ch = offset >> 5;
		channel_list[ch].waveform[offset & 0x1f] = data;

		// ４チャンネル目の書き込みなら、５チャンネル目にも書き込む
		if (ch == 3) {
			channel_list[4].waveform[offset & 0x1f] = data;
		}

	// 0x80～0x89 : freqency
	} else if (offset >= 0x80 && offset < 0x8a) {
		const BYTE ch = (offset - 0x80) / 2;
		const BYTE fofs = offset & 0xfe;
		const int freq = ((REG[fofs + 1] << 8) + REG[fofs]) & 0x0fff;

		channel_list[ch].frequency = freq;
		info[ch].keycode = m_kctbl[((REG[fofs + 1] << 8) + REG[fofs]) & 0x0fff];

	// 0x8a～0x8e : volume
	} else if (offset >= 0x8a && offset < 0x8f) {
		const BYTE ch = offset - 0x8a;
		const BYTE vol = data & 0x0f;

		channel_list[ch].volume = vol;
		//info[ch].volume = infovol[vol];
		info[ch].volume = vol * 8;

		// ついでにパンもセットしておく
		info[ch].pan = ssTrackInfo::PAN_CENTER;

		if (vol) {
			info[ch].keyon = 0xff;			// keyon
		} else {
			info[ch].keyon = 0x00;			// keyoff
		}
	// 0x8f : keyon/keyoff data
	} else if (offset == 0x8f) {
		// チャンネルワーク keyon/off の更新
		BYTE onoffbit = 0x01;
		for (int i = 0; i < MAX_PCM; i++) {
			if ((data & onoffbit) && (channel_list[0].key == 0)) {
				info[i].keyon = 0xff;			// keyon
			} else {
				info[i].keyon = 0x00;			// keyoff
			}
			onoffbit *= 2;
		}

		channel_list[0].key = data & 0x01;
		channel_list[1].key = data & 0x02;
		channel_list[2].key = data & 0x04;
		channel_list[3].key = data & 0x08;
		channel_list[4].key = data & 0x10;
	}
}

BYTE ss051649::Read(WORD _adr)
{
	return REG[_adr & 0xff];
}

int ss051649::GetBufferCount(void) const
{
	return 1;
}

WORD ss051649::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_MONO;
}

int ss051649::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ss051649::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}

int ss051649::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 0x180) {
		return 0;
	}

	if (_offset + _count >= 0x180) {
		count = 0x180 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, REG + _offset, count);

	return count;
}
