#include "StdAfx.h"
#include "ssSoundStream.h"
#include "ssSoundChip.h"
#include "ssSoundDriverManager.h"

ssSoundStream::ssSoundStream(ssSoundChip *_chip)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();

	chip = _chip;

	num_buff = _chip->GetBufferCount();

	for (int i = 0; i < num_buff; i++) {
		flag[i] = chip->GetBufferFlag(i);
		if (flag[i] & F_STEREO) {
			buff[i] = new short[config.stream_buffer_count * 2];
		} else if (flag[i] & F_STEREO_LONG) {
			buff[i] = new short[config.stream_buffer_count * 2 * 2];
		} else if (flag[i] & F_STEREO_SEPARATE_LONG) {
			buff[i] = new short[config.stream_buffer_count * 2 * 2];
		} else {
			buff[i] = new short[config.stream_buffer_count];
		}
	}
}

ssSoundStream::~ssSoundStream()
{
	for (int i = 0; i < num_buff; i++) {
		delete[] buff[i];
	}
}
