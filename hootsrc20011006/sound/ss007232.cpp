#include "StdAfx.h"
#include "sound/ss007232.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

const DWORD ss007232::MAX_007232 = 2;
const int ss007232::ADDR_SHIFT = 12;

ss007232::ss007232()
{
	memset(m_reg, 0, 16);
}

ss007232::~ss007232()
{
}

bool ss007232::Initialize(int _baseclock, BYTE *_pcmdata)
{
	int i;
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	for (i = 0; i < 512; i++) {
		m_stepdata[i] = (int)
			(((double)_baseclock / (double)sampling_rate) *
			(((512.0 * 55.0)/(512.0 - (double)i)) / 440.0) *
			(3579545.0 / 4000000.0) *
			(1<<ADDR_SHIFT));
		m_kctable[i] = Hz2Kc(((512.0 * 55.0)/(512.0 - (double)i)) * (double)_baseclock / 15600.0);
	}

	m_pcmdata = _pcmdata;

	for (i = 0; i < 2; i++) {
		m_ch[i].vol[0] = 0;
		m_ch[i].vol[1] = 0;
		m_ch[i].start = NULL;
		m_ch[i].offset = 0;
		m_ch[i].incr = 0;
		m_ch[i].bank = 0;
		m_ch[i].play = false;
		m_ch[i].loop = false;

		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "7232 #%d", i);
	}

	SetMask(0);

	return true;
}

void ss007232::Write(int _adr, BYTE _data)
{
	const int reg = _adr & 0xff;

	if (reg > 15) {
		return;
	}
	m_reg[reg] = _data;

	if (reg == 0x0c) {
		const short vol0 = (_data>>4) & 0xf;
		const short vol1 = (_data   ) & 0xf;
		m_ch[0].vol[0] = vol0;
		m_ch[0].vol[1] = vol0;
		m_ch[1].vol[0] = vol1;
		m_ch[1].vol[1] = vol1;

		info[0].volume = vol0 * 8;
		info[1].volume = vol1 * 8;
	} else if (reg == 0x0d) {
		m_ch[0].loop = ((_data & 0x01) != 0);
		m_ch[1].loop = ((_data & 0x02) != 0);
	} else {
		const int c = reg / 6;
		const int r = reg % 6;
		const int b = c * 6;
		switch (r) {
		case 0x00:
		case 0x01:
			{
				int k = ((m_reg[b+1]<<8) + m_reg[b+0]) & 0x1ff;
				m_ch[c].incr = m_stepdata[k];
				info[c].keycode = m_kctable[k];
			}
			break;
		case 0x02:
		case 0x03:
		case 0x04:
			{
//				int offset = ((m_reg[b+4] << 16) + (m_reg[b+3] << 8) + m_reg[b+2]) & 0x1ffff;
//				m_ch[c].start = m_pcmdata + m_ch[c].bank + offset;
			}
			break;
		case 0x05:
			{
				int offset = ((m_reg[b+4] << 16) + (m_reg[b+3] << 8) + m_reg[b+2]) & 0x1ffff;
				m_ch[c].start = m_pcmdata + m_ch[c].bank + offset;
				m_ch[c].play = true;
				m_ch[c].offset = 0;
				info[c].keyon = 0xff;
			}
			break;
		}
	}
}

BYTE ss007232::Read(int _adr)
{
	const int reg = _adr & 0xff;
	if (reg > 15) {
		return 0;
	}
	if (reg == 0x05) {
		int offset = ((m_reg[0x04] << 16) + (m_reg[0x03] << 8) + m_reg[0x02]) & 0x1ffff;
		m_ch[0].start = m_pcmdata + m_ch[0].bank + offset;
		m_ch[0].play = true;
		m_ch[0].offset = 0;
		info[0].keyon = 0xff;
	} else if (reg == 0x0b) {
		int offset = ((m_reg[0x0a] << 16) + (m_reg[0x09] << 8) + m_reg[0x08]) & 0x1ffff;
		m_ch[1].start = m_pcmdata + m_ch[1].bank + offset;
		m_ch[1].play = true;
		m_ch[1].offset = 0;
		info[1].keyon = 0xff;
	}

	return m_reg[reg];
}

void ss007232::SetBank(int _ch, int _bank)
{
	m_ch[_ch].bank = _bank << 17;
}

// monaural
void ss007232::SetVol(int _ch, int _p, int _vol)
{
	m_ch[_ch].vol[_p] = _vol;
	info[_ch].volume = (m_ch[_ch].vol[0] + m_ch[_ch].vol[1]) * 4;
}

// stereo
void ss007232::SetVol_stereo(int _ch, int _vol)
{
	m_ch[_ch].vol[0] = (_vol >> 4) & 0x0f;
	m_ch[_ch].vol[1] = _vol & 0x0f;

	info[_ch].volume = (m_ch[_ch].vol[0] + m_ch[_ch].vol[1]) * 4;
}

int ss007232::GetBufferCount(void) const
{
	return 2;
}

WORD ss007232::GetBufferFlag(int _b) const
{
	if (_b == 0) {
		return ssSoundStream::F_LEFT;
	} else if (_b == 1) {
		return ssSoundStream::F_RIGHT;
	}
	return 0;
}

int ss007232::GetTrackCount(void) const
{
	return 2;
}

ssTrackInfo *ss007232::GetInfo(int _track)
{
	if (_track < 2) {
		return &info[_track];
	}

	return NULL;
}

int ss007232::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (_offset >= 16) {
		return 0;
	}

	if (_offset + _count >= 16) {
		count = 16 - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_reg + _offset, count);

	return count;
}

void ss007232::Update(SHORT **_buffer, DWORD _count)
{
	int i;

	memset(_buffer[0], 0, _count * sizeof(short));
	memset(_buffer[1], 0, _count * sizeof(short));

	for (int c = 0; c < 2; c++) {
		if (m_ch[c].play) {
			const bool mute = (m_mask & (1<<c)) != 0;
			SHORT *bufL = _buffer[0];
			SHORT *bufR = _buffer[1];
			BYTE *start = m_ch[c].start;
			int offset = m_ch[c].offset;
			int incr = m_ch[c].incr;
			short vol0 = m_ch[c].vol[0];
			short vol1 = m_ch[c].vol[1];
			for (i = 0; i < _count; i++) {
				BYTE *addr = start + (offset>>ADDR_SHIFT);
				if (!mute) {
					char cdat = (char)*addr;
					int dat = ((cdat&0x7f) - 0x40) << 8;
					*bufL++ += (dat * vol0) / 15;
					*bufR++ += (dat * vol1) / 15;
				}
				unsigned short d = *addr;
				if (incr >= (1<<ADDR_SHIFT)) {
					d |= (*(addr+1) << 8);
				}
				offset += incr;
				if (d & 0x8080) {
					if (m_ch[c].loop) {
						offset &= (1<<ADDR_SHIFT) - 1;
						int b = c * 6;
						m_ch[c].start = start =
							m_pcmdata + m_ch[c].bank +
							(((m_reg[b+4] << 16) + (m_reg[b+3] << 8) + m_reg[b+2]) & 0x1ffff);
					} else {
						m_ch[c].play = false;
						info[c].keyon = 0x00;
						break;
					}
				}
			}
			m_ch[c].offset = offset;
		}
	}
}
