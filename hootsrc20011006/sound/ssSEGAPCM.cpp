#include "StdAfx.h"
#include "sound/ssSEGAPCM.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

ssSEGAPCM::ssSEGAPCM()
{
	memset(m_reg, 0, 256);
}

bool ssSEGAPCM::Initialize(int _basefreq, int _banksize, int _bankmask, BYTE *_pcmdata)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_sampling_rate = sampling_rate;
	m_basefreq = _basefreq;
	m_banksize = _banksize;
	m_bankmask = _bankmask;
	m_pcmdata = _pcmdata;

	for (int i = 0; i < MAX_PCM; i++) {
		m_keyon[i] = false;
		m_start[i] = 0;
		m_end[i] = 0;
		m_bank[i] = 0;
		m_offset[i] = 0;
		m_incr[i] = 0;

		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "SEGAPCM #%d", i);
	}

	SetMask(0);

	return true;
}

ssSEGAPCM::~ssSEGAPCM()
{
}

void ssSEGAPCM::Write(int _adr, BYTE _data)
{
	int adr = _adr & 0xff;
	m_reg[adr] = _data;

	const int channel = (adr>>3) & 0x0f;
	const int base = channel * 8;
	const int reg = adr & 0x87;

	switch (reg) {
	case 0x02:
	case 0x03:
		{
			m_reg[adr] = _data & 0x3f;
			const BYTE vol = m_reg[base+2] + m_reg[base+3];
			info[channel].volume = vol;
			if (vol == 0) {
				info[channel].pan = ssTrackInfo::PAN_OFF;
			} else {
				info[channel].pan = ssTrackInfo::PAN_CENTER + 3 * ((int)m_reg[base+3] * 2 - (int)vol) / vol;
			}
		}
		break;
	case 0x04:
	case 0x05:
		m_start[channel] = (m_reg[base+5]<<8) + m_reg[base+4];
		break;
	case 0x06:
		m_end[channel] = (m_reg[base+6]<<8) + 0xff;
		break;
	case 0x07:
		{
			m_incr[channel] = ((m_basefreq * _data)<<(INCR_SHIFT-7)) / m_sampling_rate;
			info[channel].keycode = Hz2Kc(440.0 * (double)_data / 128.0);
		}
		break;
	case 0x86:
		{
			if (_data & 1) {
				m_keyon[channel] = false;
				info[channel].keyon = 0x00;
			} else {
				//TRACE("%02x:%02x\n", reg, _data);
				m_keyon[channel] = true;
				m_offset[channel] = 0;
				m_bank[channel] = (_data & m_bankmask) * m_banksize;
				info[channel].keyon = 0xff;
			}
		}
		break;
	}
}

BYTE ssSEGAPCM::Read(int _adr)
{
	return m_reg[_adr & 0xff];
}

int ssSEGAPCM::GetBufferCount(void) const
{
	return 1;
}

WORD ssSEGAPCM::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO;
}

int ssSEGAPCM::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ssSEGAPCM::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}


int ssSEGAPCM::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 256) {
		return 0;
	}

	if (_offset + _count >= 256) {
		count = 256 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_reg + _offset, count);

	return count;
}


void ssSEGAPCM::Update(SHORT **_buffer, DWORD _count)
{
	short *buffer = _buffer[0];

	memset(buffer, 0, sizeof(short) * 2 * _count);

	for (int ch = 0; ch < MAX_PCM; ch++) {
		const short base = ch * 8;
		short *p = buffer;
		if (m_keyon[ch]) {
			//TRACE("Mix ch %d %04x:%04x\n", ch, m_start[ch], m_end[ch]);
			const bool mute = (m_mask & (1<<ch)) != 0;
			for (int c = 0; c < _count; c++) {
				WORD adr = m_start[ch] + (m_offset[ch] >> INCR_SHIFT);
				if (adr >= m_end[ch]) {
					m_keyon[ch] = false;
					m_reg[base + 0x86] |= 1;
					info[ch].keyon = 0x00;
					break;
				}
				int dat = (char)(m_pcmdata[m_bank[ch] + adr] - 0x80);
				//int dat = (char)(m_pcmdata[adr + 0x8000*3] - 0x80);
				const BYTE lv = m_reg[base + 0x02];
				const BYTE rv = m_reg[base + 0x03];
				//const BYTE cv = (lv + rv) / 4;
				if (!mute) {
					*p++ += (dat) * (lv) * 2;
					*p++ += (dat) * (rv) * 2;
				}

				m_offset[ch] += m_incr[ch];
			}
		}
	}
}
