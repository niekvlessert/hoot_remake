#include "StdAfx.h"
#include "sound/ssMemoryDump.h"

ssMemoryDump::ssMemoryDump()
{
	m_address = m_buff;
	m_size = sizeof(m_buff);
	memset(m_buff, 0, sizeof(m_buff));
}

bool ssMemoryDump::SetAddress(BYTE *_address, DWORD _size)
{
	if (_address) {
		m_address = _address;
		m_size = _size;
	} else {
		m_address = m_buff;
		m_size = sizeof(m_buff);
	}

	return true;
}

void ssMemoryDump::SetBYTE(int _offset, BYTE _data)
{
	m_buff[_offset] = _data;
}

void ssMemoryDump::SetWORD(int _offset, WORD _data)
{
	m_buff[_offset++] = _data >> 8;
	m_buff[_offset] = _data;
}

void ssMemoryDump::SetDWORD(int _offset, DWORD _data)
{
	m_buff[_offset++] = _data >> 24;
	m_buff[_offset++] = _data >> 16;
	m_buff[_offset++] = _data >> 8;
	m_buff[_offset] = _data;
}

ssMemoryDump::~ssMemoryDump()
{
}

int ssMemoryDump::GetBufferCount(void) const
{
	return 0;
}

WORD ssMemoryDump::GetBufferFlag(int _b) const
{
	return 0;
}

int ssMemoryDump::GetTrackCount(void) const
{
	return 0;
}

ssTrackInfo *ssMemoryDump::GetInfo(int _track)
{
	return NULL;
}


int ssMemoryDump::GetRegs(BYTE *_buffer, int _count, int _offset)
{
	int count;

	if (m_address == NULL || m_size == 9) {
		return 0;
	}

	if (_offset >= m_size) {
		return 0;
	}

	if (_offset + _count >= m_size) {
		count = m_size - _offset;
	} else {
		count = _count;
	}

	memcpy(_buffer, m_address + _offset, count);

	return count;
}


void ssMemoryDump::Update(SHORT **_buffer, DWORD _count)
{
	return;
}

void ssMemoryDump::SetMask(DWORD _mask)
{
}

DWORD ssMemoryDump::GetMask(void)
{
	return 0;
}
