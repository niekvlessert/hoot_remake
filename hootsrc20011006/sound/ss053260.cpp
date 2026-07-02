#include "StdAfx.h"
#include "sound/ss053260.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

/* 面倒なので MAME のソースをコピー */

static inline void logerror(char *s, ...)
{
}

#define BASE_SHIFT	16

#define INTERPOLATE_SAMPLES 0

void ss053260::InitDeltaTable( void ) {
	int		i;
	//double	base = ( double )Machine->sample_rate;
	//double	max = (double)K053260_chip.intf->clock; /* Hz */
	double	base = ( double )m_sampling_rate;
	double	max = (double)m_clock; /* Hz */
	unsigned long val;

	for( i = 0; i < 0x1000; i++ ) {
		double v = ( double )( 0x1000 - i );
		double target = max / v;
		double fixed = ( double )( 1 << BASE_SHIFT );

		if ( target && base ) {
			target = fixed / ( base / target );
			val = ( unsigned long )target;
			if ( val == 0 )
				val = 1;
		} else
			val = 1;

		delta_table[i] = val;
	}
}

inline void ss053260::check_bounds( int channel ) {
	int channel_start = ( K053260_channel[channel].bank << 16 ) + K053260_channel[channel].start;
	int channel_end = channel_start + K053260_channel[channel].size - 1;

	//if ( channel_start > K053260_chip.rom_size ) {
	if ( channel_start > m_pcmsize ) {
		logerror("K53260: Attempting to start playing past the end of the rom ( start = %06x, end = %06x ).\n", channel_start, channel_end );

		K053260_channel[channel].play = 0;

		return;
	}

	//if ( channel_end > K053260_chip.rom_size ) {
	if ( channel_end > m_pcmsize ) {
		logerror("K53260: Attempting to play past the end of the rom ( start = %06x, end = %06x ).\n", channel_start, channel_end );

		//K053260_channel[channel].size = K053260_chip.rom_size - channel_start;
		K053260_channel[channel].size = m_pcmsize - channel_start;
	}
#if LOG
	logerror("K053260: Sample Start = %06x, Sample End = %06x, Sample rate = %04lx, PPCM = %s\n", channel_start, channel_end, K053260_channel[channel].rate, K053260_channel[channel].ppcm ? "yes" : "no" );
#endif
}



ss053260::ss053260(int _idTimer)
{
	if (_idTimer) {
		m_Timer = new ssTimer(_idTimer);
		m_Timer->SetInterval(0.1);
		m_Timer->Active(false);
	} else {
		m_Timer = 0;
	}
}

ss053260::~ss053260()
{
	delete m_Timer;
	m_Timer = 0;
}

bool ss053260::Initialize(int _clock, BYTE *_pcmdata, DWORD _pcmsize)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	int sampling_rate = config.sampling_rate;

	m_sampling_rate = sampling_rate;
	m_clock = _clock;
	m_pcmdata = _pcmdata;
	m_pcmsize = _pcmsize;

	for (int i = 0; i < MAX_PCM; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "0053260 #%d", i);

	}

	m_mode = 0;

	Reset();
	memset(m_reg, 0, sizeof(m_reg));
	InitDeltaTable();

	if (m_Timer) {
		m_Timer->SetInterval(32. / (double)m_clock);
	}

	SetMask(0);

	return true;
}

void ss053260::Write(int _adr, BYTE _data)
{
	int i, t;
	//int r = offset;
	//int v = data;
	int r = _adr;
	int v = _data;

	if ( r > 0x2f ) {
		logerror("K053260: Writing past registers\n" );
		return;
	}

	//if ( Machine->sample_rate != 0 )
	//	stream_update( K053260_chip.channel, 0 );

	/* before we update the regs, we need to check for a latched reg */
	if ( r == 0x28 ) {
		//t = K053260_chip.regs[r] ^ v;
		t = m_reg[r] ^ v;

		for ( i = 0; i < 4; i++ ) {
			if ( t & ( 1 << i ) ) {
				if ( v & ( 1 << i ) ) {
					info[i].keyon = 0xff;
					K053260_channel[i].play = 1;
					K053260_channel[i].pos = 0;
					K053260_channel[i].ppcm_data = 0;
					check_bounds( i );
#if INTERPOLATE_SAMPLES
					if ( delta_table[K053260_channel[i].rate] < ( 1 << BASE_SHIFT ) )
						K053260_channel[i].steps = ( 1 << BASE_SHIFT ) / delta_table[K053260_channel[i].rate];
					else
						K053260_channel[i].steps = 0;
					K053260_channel[i].stepcount = 0;
#endif
				} else {
					info[i].keyon &= 0x80;
					K053260_channel[i].play = 0;
				}
			}
		}

		K053260_channel[0].reverse = (v>>4) & 1;
		K053260_channel[1].reverse = (v>>5) & 1;
		K053260_channel[2].reverse = (v>>6) & 1;
		K053260_channel[3].reverse = (v>>7) & 1;

		//K053260_chip.regs[r] = v;
		m_reg[r] = v;
		return;
	}

	/* update regs */
	//K053260_chip.regs[r] = v;
	m_reg[r] = v;

	/* communication registers */
	if ( r < 8 )
		return;

	/* channel setup */
	if ( r < 0x28 ) {
		int channel = ( r - 8 ) / 8;

		switch ( ( r - 8 ) & 0x07 ) {
			case 0: /* sample rate low */
				K053260_channel[channel].rate &= 0x0f00;
				K053260_channel[channel].rate |= v;
				info[channel].keycode = Hz2Kc(440.0 * (0x1000 - 0xf42) / (0x1000 - K053260_channel[channel].rate));
			break;

			case 1: /* sample rate high */
				K053260_channel[channel].rate &= 0x00ff;
				K053260_channel[channel].rate |= ( v & 0x0f ) << 8;
				info[channel].keycode = Hz2Kc(440.0 * (0x1000 - 0xf42) / (0x1000 - K053260_channel[channel].rate));
			break;

			case 2: /* size low */
				K053260_channel[channel].size &= 0xff00;
				K053260_channel[channel].size |= v;
			break;

			case 3: /* size high */
				K053260_channel[channel].size &= 0x00ff;
				K053260_channel[channel].size |= v << 8;
			break;

			case 4: /* start low */
				K053260_channel[channel].start &= 0xff00;
				K053260_channel[channel].start |= v;
			break;

			case 5: /* start high */
				K053260_channel[channel].start &= 0x00ff;
				K053260_channel[channel].start |= v << 8;
			break;

			case 6: /* bank */
				K053260_channel[channel].bank = v & 0xff;
			break;

			case 7: /* volume is 7 bits. Convert to 8 bits now. */
				info[channel].volume = v;
				K053260_channel[channel].volume = ( ( v & 0x7f ) << 1 ) | ( v & 1 );
			break;
		}

		return;
	}

	static const BYTE pan_tbl[8] = {
		7, 7, 6, 5, 4, 3, 2, 1
	};
	switch( r ) {
		case 0x2a: /* loop, ppcm */
			for ( i = 0; i < 4; i++ )
				K053260_channel[i].loop = ( v & ( 1 << i ) ) != 0;

			for ( i = 4; i < 8; i++ )
				K053260_channel[i-4].ppcm = ( v & ( 1 << i ) ) != 0;
		break;

		case 0x2c: /* pan */
			//info[0].pan = ssTrackInfo::PAN_CENTER - ((v & 7) - 4);
			//info[1].pan = ssTrackInfo::PAN_CENTER - (((v>>3) & 7) - 4);
			info[0].pan = pan_tbl[v & 7];
			info[1].pan = pan_tbl[(v>>3) & 7];
			K053260_channel[0].pan = v & 7;
			K053260_channel[1].pan = ( v >> 3 ) & 7;
		break;

		case 0x2d: /* more pan */
			//info[2].pan = ssTrackInfo::PAN_CENTER - ((v & 7) - 4);
			//info[3].pan = ssTrackInfo::PAN_CENTER - (((v>>3) & 7) - 4);
			info[2].pan = pan_tbl[v & 7];
			info[3].pan = pan_tbl[(v>>3) & 7];
			K053260_channel[2].pan = v & 7;
			K053260_channel[3].pan = ( v >> 3 ) & 7;
		break;

		case 0x2f: /* control */
			//K053260_chip.mode = v & 7;
			m_mode = v & 7;
			/* bit 0 = read ROM */
			/* bit 1 = enable sound output */
			/* bit 2 = unknown */
		break;
	}
}

BYTE ss053260::Read(int _adr)
{
	const int offset = _adr;

	switch ( offset ) {
		case 0x29: /* channel status */
			{
				int i, status = 0;

				for ( i = 0; i < 4; i++ )
					status |= K053260_channel[i].play << i;

				return status;
			}
		break;

		case 0x2e: /* read rom */
			//if ( K053260_chip.mode & 1 ) {
			if ( m_mode & 1 ) {
				unsigned long offs = K053260_channel[0].start + ( K053260_channel[0].pos >> BASE_SHIFT ) + ( K053260_channel[0].bank << 16 );

				K053260_channel[0].pos += ( 1 << 16 );

				//if ( offs > K053260_chip.rom_size ) {
				if ( offs > m_pcmsize ) {
					logerror("K53260: Attempting to read past rom size on rom Read Mode.\n" );

					return 0;
				}

				//return K053260_chip.rom[offs];
				return m_pcmdata[offs];
			}
		break;
	}

	//return K053260_chip.regs[offset];
	return m_reg[offset];
}

void ss053260::Reset(void)
{
	int i;

	for( i = 0; i < 4; i++ ) {
		K053260_channel[i].rate = 0;
		K053260_channel[i].size = 0;
		K053260_channel[i].start = 0;
		K053260_channel[i].bank = 0;
		K053260_channel[i].volume = 0;
		K053260_channel[i].play = 0;
		K053260_channel[i].pan = 0;
		K053260_channel[i].pos = 0;
		K053260_channel[i].loop = 0;
		K053260_channel[i].ppcm = 0;
		K053260_channel[i].ppcm_data = 0;
		K053260_channel[i].reverse = 0;
	}
}

void ss053260::EnableIRQ(bool _b)
{
	if (m_Timer) {
		m_Timer->Active(_b);
		m_Timer->SetInterval(32. / (double)m_clock);
	}
}

int ss053260::GetBufferCount(void) const
{
	return 1;
}

WORD ss053260::GetBufferFlag(int _b) const
{
	return ssSoundStream::F_STEREO_LONG;
}

int ss053260::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ss053260::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}


int ss053260::GetRegs(BYTE *_buffer, int _count, int _offset)
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


void ss053260::Update(SHORT **_buffer, DWORD _count)
{
	int *buffer = (int *)(_buffer[0]);
	const int length = _count;

	memset(buffer, 0, _count * sizeof(int) * 2);

	static long dpcmcnv[] = { 0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1 };

	int i, j, lvol[4], rvol[4], play[4], loop[4], ppcm_data[4], ppcm[4];
	int reverse[4];
	unsigned char *rom[4];
	unsigned long delta[4], end[4], pos[4];
	int dataL, dataR;
	signed char d;
#if INTERPOLATE_SAMPLES
	int steps[4], stepcount[4];
#endif

	/* precache some values */
	for ( i = 0; i < 4; i++ ) {
		//rom[i]= &K053260_chip.rom[K053260_channel[i].start + ( K053260_channel[i].bank << 16 )];
		rom[i]= &m_pcmdata[K053260_channel[i].start + ( K053260_channel[i].bank << 16 )];
		delta[i] = delta_table[K053260_channel[i].rate];
		lvol[i] = K053260_channel[i].volume * K053260_channel[i].pan;
		rvol[i] = K053260_channel[i].volume * ( 8 - K053260_channel[i].pan );
		end[i] = K053260_channel[i].size;
		pos[i] = K053260_channel[i].pos;
		play[i] = K053260_channel[i].play;
		loop[i] = K053260_channel[i].loop;
		ppcm[i] = K053260_channel[i].ppcm;
		ppcm_data[i] = K053260_channel[i].ppcm_data;
		reverse[i] = K053260_channel[i].reverse;
#if INTERPOLATE_SAMPLES
		steps[i] = K053260_channel[i].steps;
		stepcount[i] = K053260_channel[i].stepcount;
#endif
		if ( ppcm[i] ) {
			delta[i] /= 2;
#if INTERPOLATE_SAMPLES
			steps[i] *= 2;
#endif
		}
	}

		for ( j = 0; j < length; j++ ) {

			dataL = dataR = 0;

			for ( i = 0; i < 4; i++ ) {
				/* see if the voice is on */
				if ( play[i] ) {
					const bool mute = (m_mask & (1<<i)) != 0;
					/* see if we're done */
					if ( ( pos[i] >> BASE_SHIFT ) >= end[i] ) {

						ppcm_data[i] = 0;

						if ( loop[i] )
							pos[i] = 0;
						else {
							info[i].keyon &= 0x80;
							play[i] = 0;
							continue;
						}
					}

					if ( ppcm[i] ) { /* Packed PCM */
						/* we only update the signal if we're starting or a real sound sample has gone by */
						/* this is all due to the dynamic sample rate convertion */
						if ( pos[i] == 0 || ( ( pos[i] ^ ( pos[i] - delta[i] ) ) & 0x8000 ) == 0x8000 ) {
							int newdata;
							if ( pos[i] & 0x8000 )
								newdata = rom[i][pos[i] >> BASE_SHIFT] & 0x0f;
							else
								newdata = ( ( rom[i][pos[i] >> BASE_SHIFT] ) >> 4 ) & 0x0f;

							ppcm_data[i] = ( ( ppcm_data[i] * 62 ) >> 6 ) + dpcmcnv[newdata];

							if ( ppcm_data[i] > 127 )
								ppcm_data[i] = 127;
							else
								if ( ppcm_data[i] < -128 )
									ppcm_data[i] = -128;
						}

						d = ppcm_data[i];

//						d /= 2;

#if INTERPOLATE_SAMPLES
						if ( steps[i] ) {
							if ( ( pos[i] >> BASE_SHIFT ) < ( end[i] - 1 ) ) {
								signed char diff;
								int next_d;

								if ( pos[i] & 0x8000 )
									next_d = ( ( ( rom[i][(pos[i] >> BASE_SHIFT)+1] ) >> 4 ) & 0x0f ) * 0x11;
								else
									next_d = ( rom[i][( pos[i] >> BASE_SHIFT)] & 0x0f ) * 0x11;

								diff = next_d;
								diff /= 2;
								diff -= d;

								diff /= steps[i];

								d += ( diff * stepcount[i]++ );

								if ( stepcount[i] >= steps[i] )
									stepcount[i] = 0;
							}
						}
#endif
						pos[i] += delta[i];
					} else { /* PCM */
						if (!reverse[i]) {
							d = rom[i][pos[i] >> BASE_SHIFT];
						} else {
							d = rom[i][-(pos[i] >> BASE_SHIFT)];
						}

#if INTERPOLATE_SAMPLES
						if ( steps[i] ) {
							if ( ( pos[i] >> BASE_SHIFT ) < ( end[i] - 1 ) ) {
								signed char diff = rom[i][(pos[i] >> BASE_SHIFT) + 1];
								diff -= d;
								diff /= steps[i];

								d += ( diff * stepcount[i]++ );

								if ( stepcount[i] >= steps[i] )
									stepcount[i] = 0;
							}
						}
#endif

						pos[i] += delta[i];
					}

					//if ( K053260_chip.mode & 2 ) {
					if ( m_mode & 2 ) {
						if (!mute) {
							dataL += ( d * lvol[i] ) >> 2;
							dataR += ( d * rvol[i] ) >> 2;
						}
					}
				}
			}

			//buffer[1][j] = limit( dataL, MAXOUT, MINOUT );
			//buffer[0][j] = limit( dataR, MAXOUT, MINOUT );
			*buffer++ = dataL;
			*buffer++ = dataR;
		}

	/* update the regs now */
	for ( i = 0; i < 4; i++ ) {
		K053260_channel[i].pos = pos[i];
		K053260_channel[i].play = play[i];
		K053260_channel[i].ppcm_data = ppcm_data[i];
#if INTERPOLATE_SAMPLES
		K053260_channel[i].stepcount = stepcount[i];
#endif
	}
}
