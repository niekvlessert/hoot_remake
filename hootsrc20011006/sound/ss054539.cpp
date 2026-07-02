#include "StdAfx.h"
#include "sound/ss054539.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

// based on mame/src/sound/k054539.c

#define logerror TRACE

ss054539::ss054539(int _timer)
{
	if (_timer != 0) {
		m_timer = new ssTimer(_timer);
		m_timer->SetInterval(1. / 500.);
	} else {
		m_timer = NULL;
	}

	m_handler = NULL;
}

ss054539::~ss054539()
{
	delete m_timer;
}


int ss054539::regupdate(void)
{
	return !(regs[0x22f] & 0x80);
}

void ss054539::keyon(int channel)
{
	info[channel].keyon = 0xff;
	if(regupdate())
		regs[0x22c] |= 1 << channel;
}

void ss054539::keyoff(int channel)
{
	info[channel].keyon &= 0x80;
	if(regupdate())
		regs[0x22c] &= ~(1 << channel);
}

bool ss054539::Initialize(int _clock, BYTE *_pcmdata, DWORD _pcmsize)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	freq_ratio = (double)_clock / (double)sampling_rate;

	for (int i = 0; i < MAX_PCM; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "0054539 #%d", i);
	}

	// Factor the 1/4 for the number of channels in the volume (1/8 is too harsh, 1/2 gives clipping)
	// vol=0 -> no attenuation, vol=0x40 -> -36dB
	for(i=0; i<256; i++)
		voltab[i] = pow(10.0, (-36.0 * (double)i / (double)0x40) / 20.0) / 4.0 * 2.;

	// Pan table for the left channel
	// Right channel is identical with inverted index
	// Formula is such that pan[i]**2+pan[0xe-i]**2 = 1 (constant output power)
	// and pan[0] = 1 (full panning)
	for(i=0; i<0xf; i++)
		pantab[i] = sqrt(0xe - i) / sqrt(0xe);

	memset(regs, 0, sizeof(regs));
	//ram = malloc(0x4000);
	memset(ram, 0, sizeof(ram));
	cur_ptr = 0;
	rom = _pcmdata;
	rom_size = _pcmsize;
	mrom_mask = 0xffffffffU;
	for(i=0; i<32; i++)
		if((1U<<i) >= rom_size) {
			mrom_mask = (1U<<i) - 1;
			break;
		}

	SetMask(0);

	return true;
}

void ss054539::SetPanHandler(ss054539PanHandler _handler, void *_param)
{
	m_handler = _handler;
	m_handler_param = _param;
}

void ss054539::Write(int _adr, BYTE _data)
{
	const int offset = _adr;
	const int data = _data;

	if (offset > 0x22f) return;

	//TRACE("W [%03x] %02x\n", offset, data);
	BYTE old = regs[offset];
	regs[offset] = data;
	if (offset < 0x100) {
		BYTE ch = offset / 0x20;
		BYTE *reg = &regs[ch * 0x20];
		switch (offset & 0x1f) {
		case 0x00:
		case 0x01:
		case 0x02:
			info[ch].keycode = Hz2Kc(440.0 * (reg[0x00] | (reg[0x01] << 8) | (reg[0x02] << 16)) / 0x962f);
			break;
		case 0x03:
			info[ch].volume = 0x7f - (data & 0x7f);
			break;
		case 0x05:
			{
				int pantable[15] = {
					1,1,1,2,2,3,3,
					4,
					5,5,6,6,7,7,7
				};
				int pan = data >= 0x11 && data <= 0x1f ?
					data - 0x11 : 0x18 - 0x11;
				info[ch].pan = pantable[pan];
			}
			break;
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0c:
		case 0x0d:
		case 0x0e:
			if (regs[0x22c] & (1<<ch)) {
				//TRACE("during keyon %d\n",ch);
				regs[offset] = old;
			}
			break;
		}
	}
	switch(offset) {
	case 0x13f: {
		int pan = data >= 0x11 && data <= 0x1f ? data - 0x11 : 0x18 - 0x11;
		//if(K054539_chips.intf->apan[chip])
		//	K054539_chips.intf->apan[chip](K054539_chips.pantab[pan], K054539_chips.pantab[0xe - pan]);
		if (m_handler != NULL) {
			m_handler(1, pantab[pan], pantab[0x0e - pan], m_handler_param);
		}
		break;
	}
	case 0x17f: {
		int pan = data >= 0x11 && data <= 0x1f ? data - 0x11 : 0x18 - 0x11;
		//if(K054539_chips.intf->apan[chip])
		//	K054539_chips.intf->apan[chip](K054539_chips.pantab[pan], K054539_chips.pantab[0xe - pan]);
		if (m_handler != NULL) {
			m_handler(0, pantab[pan], pantab[0x0e - pan], m_handler_param);
		}
		break;
	}
	case 0x214: {
		int ch;
		for(ch=0; ch<8; ch++)
			if(data & (1<<ch))
				keyon(ch);
		break;
	}
	case 0x215: {
		int ch;
		for(ch=0; ch<8; ch++)
			if(data & (1<<ch))
				keyoff(ch);
		break;
	}
	case 0x22d:
		if(regs[0x22e] == 0x80)
			cur_zone[cur_ptr] = data;
		cur_ptr++;
		if(cur_ptr == cur_limit)
			cur_ptr = 0;
		break;
	case 0x22e:
		cur_zone =
			data == 0x80 ? ram :
			rom + 0x20000*data;
		cur_limit = data == 0x80 ? 0x4000 : 0x20000;
		cur_ptr = 0;
		break;
	case 0x22f:
		if (m_timer != NULL) {
			if (data & 0x20) {
				m_timer->Active(true);
			} else {
				m_timer->Active(false);
			}
		}
		break;
	default:
		if(old != data) {
			if((offset & 0xff00) == 0) {
				int chanoff = offset & 0x1f;
				if(chanoff < 4 || chanoff == 5 ||
				   (chanoff >=8 && chanoff <= 0xa) ||
				   (chanoff >= 0xc && chanoff <= 0xe))
					break;
			}
			if(1 || ((offset >= 0x200) && (offset <= 0x210)))
				break;
			logerror("K054539 %03x = %02x\n", offset, data);
		}
		break;
	}
}

BYTE ss054539::Read(int _adr)
{
	const int offset = _adr;

	if (offset > 0x22f) return 0xff;

	//TRACE("R [%03x] %02x\n", offset, regs[offset]);
	switch(offset) {
	case 0x22d:
		if(regs[0x22f] & 0x10) {
			BYTE res = cur_zone[cur_ptr];
			cur_ptr++;
			if(cur_ptr == cur_limit)
				cur_ptr = 0;
			return res;
		} else
			return 0;
	case 0x22c:
		break;
	default:
		logerror("K054539 read %03x\n", offset);
		break;
	}
	return regs[offset];
}

void ss054539::Reset(void)
{
}

int ss054539::GetBufferCount(void) const
{
	return 1;
}

WORD ss054539::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ss054539::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ss054539::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}


int ss054539::GetRegs(BYTE *_buffer, int _count, int _offset)
{
#if 0
	/* 0x200-0x22f ‚ð 0x100 ‚ÉƒRƒs[‚·‚é”Å */
	BYTE tmp[0x100 + 0x30];
	memcpy(tmp, regs, 256);
	memcpy(tmp+0x100, regs+0x200, 0x30);
	const int size = sizeof(tmp);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, tmp + _offset, count);

	return count;
#else
	const int size = sizeof(regs);
	int count;

	if (_offset >= size) {
		return 0;
	}

	if (_offset + _count >= size) {
		count = size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, regs + _offset, count);

	return count;
#endif
}


void ss054539::Update(SHORT **_buffer, DWORD _count)
{
	int *buffer = (int *)(_buffer[0]);
	const int length = _count;

	static INT16 dpcm[16] = {
		0<<8, 1<<8, 4<<8, 9<<8, 16<<8, 25<<8, 36<<8, 49<<8,
		-64<<8, -49<<8, -36<<8, -25<<8, -16<<8, -9<<8, -4<<8, -1<<8
	};

	int ch;
	unsigned char *samples;
	UINT32 rom_mask;

	memset(buffer, 0, _count * sizeof(int) * 2);

	if(!(regs[0x22f] & 1))
		return;

	samples = rom;
	rom_mask = mrom_mask;

	for(ch=0; ch<8; ch++)
		if(regs[0x22c] & (1<<ch)) {
			const bool mute = (m_mask & (1<<ch)) != 0;
			unsigned char *base1 = regs + 0x20*ch;
			unsigned char *base2 = regs + 0x200 + 0x2*ch;
			struct K054539_channel *chan = channels + ch;

			int *buf = buffer;

			UINT32 cur_pos = (base1[0x0c] | (base1[0x0d] << 8) | (base1[0x0e] << 16)) & rom_mask;
			INT32 cur_pfrac;
			INT32 cur_val, cur_pval, rval;

			INT32 delta = (base1[0x00] | (base1[0x01] << 8) | (base1[0x02] << 16)) * freq_ratio;
			INT32 fdelta;
			int pdelta;

			int pan = base1[0x05] >= 0x11 && base1[0x05] <= 0x1f ? base1[0x05] - 0x11 : 0x18 - 0x11;
			int vol = base1[0x03] & 0x7f;
			double lvol = voltab[vol] * pantab[pan];
			double rvol = voltab[vol] * pantab[0xe - pan];

			if(base2[0] & 0x20) {
				delta = -delta;
				fdelta = +0x10000;
				pdelta = -1;
			} else {
				fdelta = -0x10000;
				pdelta = +1;
			}

			if(cur_pos != chan->pos) {
				chan->pos = cur_pos;
				cur_pfrac = 0;
				cur_val = 0;
				cur_pval = 0;
			} else {
				cur_pfrac = chan->pfrac;
				cur_val = chan->val;
				cur_pval = chan->pval;
			}

#define UPDATE_CHANNELS																	\
			do {																		\
				rval = (cur_pval*cur_pfrac + cur_val*(0x10000 - cur_pfrac)) >> 16;		\
				if (!mute) {															\
					*buf++ += (rval*lvol);												\
					*buf++ += (rval*rvol);												\
				}																		\
			} while(0)

			switch(base2[0] & 0xc) {
			case 0x0: { // 8bit pcm
				int i;
				for(i=0; i<length; i++) {
					cur_pfrac += delta;
					while(cur_pfrac & ~0xffff) {
						cur_pfrac += fdelta;
						cur_pos += pdelta;

						cur_pval = cur_val;
						cur_val = (INT16)(samples[cur_pos] << 8);
						if(cur_val == (INT16)0x8000) {
							if(base2[1] & 1) {
								cur_pos = (base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16)) & rom_mask;
								cur_val = (INT16)(samples[cur_pos] << 8);
								if(cur_val != (INT16)0x8000)
									continue;
							}
							keyoff(ch);
							goto end_channel_0;
						}
					}

					UPDATE_CHANNELS;
				}
			end_channel_0:
				break;
			}
			case 0x4: { // 16bit pcm lsb first
				int i;
				//cur_pos >>= 1;

				for(i=0; i<length; i++) {
					cur_pfrac += delta;
					while(cur_pfrac & ~0xffff) {
						cur_pfrac += fdelta;
						//cur_pos += pdelta;
						cur_pos += pdelta*2;

						cur_pval = cur_val;
						//cur_val = (INT16)(samples[cur_pos<<1] | samples[(cur_pos<<1)|1]<<8);
						cur_val = (INT16)(samples[cur_pos] | samples[cur_pos+1]<<8);
						if(cur_val == (INT16)0x8000) {
							if(base2[1] & 1) {
								//cur_pos = ((base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16)) & rom_mask) >> 1;
								cur_pos = (base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16)) & rom_mask;
								//cur_val = (INT16)(samples[cur_pos<<1] | samples[(cur_pos<<1)|1]<<8);
								cur_val = (INT16)(samples[cur_pos] | samples[cur_pos+1]<<8);
								if(cur_val != (INT16)0x8000)
									continue;
							}
							keyoff(ch);
							goto end_channel_4;
						}
					}

					UPDATE_CHANNELS;
				}
			end_channel_4:
				//cur_pos <<= 1;
				break;
			}
			case 0x8: { // 4bit dpcm
				int i;

				cur_pos <<= 1;
				cur_pfrac <<= 1;
				if(cur_pfrac & 0x10000) {
					cur_pfrac &= 0xffff;
					cur_pos |= 1;
				}

				for(i=0; i<length; i++) {
					cur_pfrac += delta;
					while(cur_pfrac & ~0xffff) {
						cur_pfrac += fdelta;
						cur_pos += pdelta;

						cur_pval = cur_val;
						cur_val = samples[cur_pos>>1];
						if(cur_val == 0x88) {
							if(base2[1] & 1) {
								cur_pos = ((base1[0x08] | (base1[0x09] << 8) | (base1[0x0a] << 16)) & rom_mask) << 1;
								cur_val = samples[cur_pos>>1];
								if(cur_val != 0x88)
									goto next_iter;
							}
							keyoff(ch);
							goto end_channel_8;
						}
					next_iter:
						if(cur_pos & 1)
							cur_val >>= 4;
						else
							cur_val &= 15;
						cur_val = cur_pval + dpcm[cur_val];
						if(cur_val < -32768)
							cur_val = -32768;
						else if(cur_val > 32767)
							cur_val = 32767;
					}

					UPDATE_CHANNELS;
				}
			end_channel_8:
				cur_pfrac >>= 1;
				if(cur_pos & 1)
					cur_pfrac |= 0x8000;
				cur_pos >>= 1;
				break;
			}
			default:
				logerror("Unknown sample type %x for channel %d\n", base2[0] & 0xc, ch);
				break;
			}
			chan->pos = cur_pos;
			chan->pfrac = cur_pfrac;
			chan->pval = cur_pval;
			chan->val = cur_val;
			if(regupdate()) {
				base1[0x0c] = cur_pos & 0xff;
				base1[0x0d] = (cur_pos >> 8) & 0xff;
				base1[0x0e] = (cur_pos >> 16) & 0xff;
			}
		}
}
