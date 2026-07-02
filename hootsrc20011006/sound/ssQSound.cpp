#include "StdAfx.h"
#include "sound/ssQSound.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

/* mame/src/sound/qsound.c */

ssQSound::ssQSound()
{
	memset(m_reg, 0, sizeof(m_reg));
}

ssQSound::~ssQSound()
{
}

bool ssQSound::Initialize(int _clock, BYTE *_pcmdata)
{
	int i;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	sample_rate = config.sampling_rate;

	qsound_sample_rom = (QSOUND_SRC_SAMPLE *)_pcmdata;

	memset(qsound_channel, 0, sizeof(qsound_channel));

	qsound_frq_ratio = ((float)_clock / (float)QSOUND_CLOCKDIV) /
						(float)sample_rate;
	qsound_frq_ratio *= 16.0;

	/* Create pan table */
	for (i=0; i<33; i++)
	{
		qsound_pan_table[i]=(int)((256/sqrt(32)) * sqrt(i));
	}

	for (i = 0; i < MAX_VOICE; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "QSOUND #%d", i);
	}

	SetMask(0);

	return true;
}

void ssQSound::WriteCommand(BYTE _data)
{
	const BYTE data = _data;
	const int value = qsound_data;

	int ch=0,reg=0;
	if (data < 0x80)
	{
		ch=data>>3;
		reg=data & 0x07;
	}
	else
	{
		if (data < 0x90)
		{
			ch=data-0x80;
			reg=8;
		}
		else
		{
			if (data >= 0xba && data < 0xca)
			{
				ch=data-0xba;
				reg=9;
			}
			else
			{
				/* Unknown registers */
				return;
			}
		}
	}

	m_reg[ch*32 + reg*2 + 0] = value >> 8;
	m_reg[ch*32 + reg*2 + 1] = value;

	switch (reg)
	{
		case 0: /* Bank */
			ch=(ch+1)&0x0f;	/* strange ... */
			qsound_channel[ch].bank=(value&0x7f)<<16;
			qsound_channel[ch].bank /= LENGTH_DIV;
			break;
		case 1: /* start */
			qsound_channel[ch].address=value;
			qsound_channel[ch].address/=LENGTH_DIV;
			break;
		case 2: /* pitch */
			qsound_channel[ch].pitch=(long)
					((float)value * qsound_frq_ratio );
			qsound_channel[ch].pitch/=LENGTH_DIV;
			info[ch].keycode = Hz2Kc(440 * value / 0x0d70);
			if (!value)
			{
				/* Key off */
				qsound_channel[ch].key=0;
				info[ch].keyon &= 0x80;
			}
			break;
		case 3: /* unknown */
			qsound_channel[ch].reg3=value;
			break;
		case 4: /* loop offset */
			qsound_channel[ch].loop=value/LENGTH_DIV;
			break;
		case 5: /* end */
			qsound_channel[ch].end=value/LENGTH_DIV;
			break;
		case 6: /* master volume */
			if (value==0)
			{
				/* Key off */
				qsound_channel[ch].key=0;
				info[ch].keyon &= 0x80;
			}
			else if (qsound_channel[ch].key==0)
			{
				/* Key on */
				qsound_channel[ch].key=1;
				qsound_channel[ch].offset=0;
				qsound_channel[ch].lastdt=0;
				info[ch].keyon = 0xff;
			}
			qsound_channel[ch].vol=value;
			{
				int vol = value / 16;
				if (vol > 127) vol = 127;
				info[ch].volume = vol;
			}
			break;

		case 7:  /* unused */
			break;
		case 8:
			{
				static const int pantbl[] = {
					1,1,1,1,1, //  0 - 4
					2,2,2,2,2, //  5 - 9
					3,3,3,3,3, // 10 - 14
					4,4,4,     // 15 - 17
					5,5,5,5,5, // 18 - 22
					6,6,6,6,6, // 23 - 27
					7,7,7,7,7, // 28 - 32
				};
				int pandata=(value-0x10)&0x3f;
				if (pandata > 32)
				{
					pandata=32;
				}
				qsound_channel[ch].rvol=qsound_pan_table[pandata];
				qsound_channel[ch].lvol=qsound_pan_table[32-pandata];
				qsound_channel[ch].pan = value;
				info[ch].pan = pantbl[pandata];
			}
			break;
		 case 9:
			qsound_channel[ch].reg9=value;
			break;
	}
}

void ssQSound::Write(int _adr, BYTE _data)
{
	const int offset = _adr & 1;
	const BYTE data = _data;

	if (offset == 0) {
		qsound_data = (qsound_data & 0x00ff) | (data << 8);
	} else {
		qsound_data = (qsound_data & 0xff00) | data;
	}
}

BYTE ssQSound::Read(int _adr)
{
	return 0x80;
}

int ssQSound::GetBufferCount(void) const
{
	return 1;
}

WORD ssQSound::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ssQSound::GetTrackCount(void) const
{
	return MAX_VOICE;
}

ssTrackInfo *ssQSound::GetInfo(int _track)
{
	if (_track < MAX_VOICE) {
		return &info[_track];
	}

	return NULL;
}

int ssQSound::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	const int size = sizeof(m_reg);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_reg + _offset, count);

	return count;
}


void ssQSound::Update(SHORT **_buffer, DWORD _count)
{
	QSOUND_SAMPLE *buffer = (int *)(_buffer[0]);
	const int length = _count;

	int i,j;
	struct QSOUND_CHANNEL *pC=&qsound_channel[0];
	QSOUND_SRC_SAMPLE * pST;

	memset(buffer, 0, sizeof(QSOUND_SAMPLE) * 2 * _count);

	for (i=0; i<QSOUND_CHANNELS; i++)
	{
		if (pC->key)
		{
			const bool mute = (m_mask & (1<<i)) != 0;
			QSOUND_SAMPLE *pOut=buffer;
			pST=qsound_sample_rom+pC->bank;
			const int rvol=(pC->rvol*pC->vol)>>(8*LENGTH_DIV);
			const int lvol=(pC->lvol*pC->vol)>>(8*LENGTH_DIV);
			int address = pC->address;
			const int pitch = pC->pitch;
			const int end = pC->end;
			const int loop = pC->loop;
			int lastdt = pC->lastdt;
			int offset = pC->offset;

			for (j=length-1; j>=0; j--)
			{
				const int count=offset>>16;
				offset &= 0xffff;
				if (count)
				{
					address += count;
					if (address >= end)
					{
						if (!loop)
						{
							/* Reached the end of a non-looped sample */
							pC->key=0;
							info[i].keyon &= 0x80;
							break;
						}
						/* Reached the end, restart the loop */
						address = (end - loop) & 0xffff;
					}
					lastdt=pST[address];
				}

				if (!mute) {
					*pOut++ += ((lastdt * lvol) >> 6);
					*pOut++ += ((lastdt * rvol) >> 6);
				}
				offset += pitch;
			}
			pC->address = address;
			pC->lastdt = lastdt;
			pC->offset = offset;
		}
		pC++;
	}
}
