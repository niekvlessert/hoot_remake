#include "StdAfx.h"
#include "sound/ssC30.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

/* ノイズ部は MAME のソースを参考 */

ssC30::ssC30(int _type)
{
	m_type = _type;
	m_stereo = false;

	memset(m_reg, 0, sizeof(m_reg));
}

ssC30::~ssC30()
{
}

bool ssC30::Initialize(int _basefreq, BYTE *_pcmdata)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_sampling_rate = sampling_rate;
	m_basefreq = _basefreq;
	m_pcmdata = _pcmdata;

	m_enable = true;

	for (int i = 0; i < MAX_PCM; i++) {
		m_ch[i].freq = 0;
		m_ch[i].voll = 0;
		m_ch[i].volr = 0;
		m_ch[i].wave = m_pcmdata;
		m_ch[i].offset = 0;
		m_ch[i].incr = 0;

		m_ch[i].noise = 0;
		m_ch[i].noise_state = 0;
		m_ch[i].noise_counter = 0;
		m_ch[i].noise_seed = 1;

		InitTrackInfo(&info[i]);

		switch (m_type) {
		case TYPE_STEREO:
			m_stereo = true;
			sprintf(info[i].name, "C30 #%d", i);
			break;
		case TYPE_MONO:
			m_stereo = false;
			sprintf(info[i].name, "C30 #%d", i);
			break;
		case TYPE_WSG:
			m_stereo = false;;
			sprintf(info[i].name, "WSG #%d", i);
			break;
		default:
			sprintf(info[i].name, "??? #%d", i);
			break;
		}
	}

	SetMask(0);

	return true;
}

void ssC30::Write(int _adr, BYTE _data)
{
	static const BYTE pantbl[31] = {
		1,1,1,1,1,1,1,1,1,2,2,3,3,4,
		4,
		4,5,5,6,6,7,7,7,7,7,7,7,7,7
	};
	int adr = _adr & 0xff;

	if (adr >= 0x40) return;

	m_reg[adr] = _data;

	const int channel = adr>>3;
	struct channel *ch = &m_ch[channel];
	const int reg = adr & 7;

	switch (reg) {
	case 4:
		m_ch[(channel+1) % MAX_PCM].noise =  (_data & 0x80);
		// fall down
	case 0:
		{
			int l, r;
			l = ch->voll = (m_reg[channel*8 + 0] & 0x0f);
			if (m_stereo) {
				r = ch->volr = (m_reg[channel*8 + 4] & 0x0f);
			} else {
				r = ch->volr = (m_reg[channel*8 + 0] & 0x0f);
			}
			int p = (r - l) + 15;
			info[channel].pan = pantbl[p];
			info[channel].volume = l > r ? l<<3 : r<<3;
			if (l == 0 && r == 0) {
				info[channel].keyon &= 0x80;
			} else {
				info[channel].keyon = 0xff;
			}
		}
		break;
	case 1:
		ch->wave = m_pcmdata + (_data >> 4) * 16;
	case 2:
	case 3:
		{
			ch->freq = ((m_reg[channel*8+1]&0xf)<<16) + (m_reg[channel*8+2]<<8) + (m_reg[channel*8+3]);
			//ch->incr = ch->freq * m_basefreq / m_sampling_rate / (0x10000 / 0x8000);
			ch->incr = (double)ch->freq * m_basefreq * 2 / m_sampling_rate;
			info[channel].keycode = Hz2Kc(ch->freq * m_basefreq / (0x4000*32));
		}
		break;
	}
}

void ssC30::WriteMAPPY(int _adr, BYTE _data)
{
	int adr = _adr & 0xff;

	if (adr >= 0x40) return;

	m_reg[adr] = _data;

	const int channel = adr>>3;
	struct channel *ch = &m_ch[channel];
	const int reg = adr & 7;

	switch (reg) {
	case 3:
		{
			int v = _data & 0x0f;
			ch->voll = v;
			ch->volr = v;
			info[channel].pan = ssTrackInfo::PAN_CENTER;
			info[channel].volume = v << 3;
			if (v == 0) {
				info[channel].keyon &= 0x80;
			} else {
				info[channel].keyon = 0xff;
			}
		}
		break;
	case 6:
		ch->wave = m_pcmdata + ((_data >> 4)&7) * 16;
	case 4:
	case 5:
		{
			ch->freq = (((m_reg[channel*8+6]&0xf)<<16) + (m_reg[channel*8+5]<<8) + (m_reg[channel*8+4]));
			//ch->incr = ch->freq * m_basefreq / m_sampling_rate / (0x10000 / 0x8000);
			ch->incr = (double)ch->freq * m_basefreq * 2 / m_sampling_rate;
			info[channel].keycode = Hz2Kc(ch->freq * m_basefreq / (0x4000*32));
		}
		break;
	}
}

BYTE ssC30::Read(int _adr)
{
	return m_reg[_adr & 0x3f];
}

void ssC30::Enable(bool _enable)
{
	m_enable = _enable;
}


int ssC30::GetBufferCount(void) const
{
	return 1;
}

WORD ssC30::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO;
}

int ssC30::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ssC30::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}


int ssC30::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 0x40) {
		return 0;
	}

	if (_offset + _count >= 0x40) {
		count = 0x40 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_reg + _offset, count);

	return count;
}


void ssC30::Update(SHORT **_buffer, DWORD _count)
{
	memset(_buffer[0], 0, sizeof(short) * 2 * _count);

	if (!m_enable) return;

	for (int i = 0; i < MAX_PCM; i++) {
		struct channel *ch = &m_ch[i];
		const int vl = ch->voll;
		const int vr = ch->volr;
		const bool mute = (m_mask & (1<<i)) != 0;
		short *buffer = _buffer[0];
		if (ch->noise == 0) {
			// tone
			if ((vl||vr) && ch->freq) {
				for (DWORD count = _count; count != 0; count--) {
					int p = (ch->offset >> 16) & 0x1f;
					char d;
					if (p&1) {
						d = (char)(ch->wave[p>>1] & 0x0f) - 8;
					} else {
						d = (char)(ch->wave[p>>1] >> 4) - 8;
					}
					d <<= 4;
					if (!mute) {
						*buffer++ += (int)d * vl;
						*buffer++ += (int)d * vr;
						ch->offset += ch->incr;
					}
				}
			}
		} else {
			// noise
			if ((vl||vr) && (ch->freq&0xff)) {
				//float fb = (float)m_sampling_rate / (float)m_basefreq;
				//int delta = ((ch->freq&0xff)<<4) * fb;
				int delta = ((ch->freq&0xff)<<4) * m_sampling_rate / m_basefreq;
				int c = ch->noise_counter;
				for (DWORD count = _count; count != 0; count--) {
					int d;
					int cnt;

					if (ch->noise_state) {
						d = 0x07 * 16;
					} else {
						d = -0x08 * 16;
					}
					if (!mute) {
						*buffer++ += (int)d * vl;
						*buffer++ += (int)d * vr;
					}

					c += delta;
					cnt = c >> 12;
					c &= (1<<12) - 1;
					for (; cnt > 0; cnt--) {
						if ((ch->noise_seed + 1) & 2) ch->noise_state ^= 1;
						if (ch->noise_seed & 1) ch->noise_seed ^= 0x28000;
						ch->noise_seed >>= 1;
					}
				}
				ch->noise_counter = c;
			}
		}
	}

}

