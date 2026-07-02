#include "StdAfx.h"
#include "sound/ssC140.h"
#include "ssConfig.h"
#include "ssSoundDriverManager.h"
#include "ssSoundStream.h"

/* Ž©•ª‚Å‘‚­‚Ì‚ª–Ê“|‚È‚Ì‚Å MAME ‚©‚çƒpƒNƒŠ */

struct voice_registers
{
	BYTE volume_right;
	BYTE volume_left;
	BYTE frequency_msb;
	BYTE frequency_lsb;
	BYTE bank;
	BYTE mode;
	BYTE start_msb;
	BYTE start_lsb;
	BYTE end_msb;
	BYTE end_lsb;
	BYTE loop_msb;
	BYTE loop_lsb;
	BYTE reserved[4];
};

void ssC140::init_voice( VOICE *v )
{
	v->key=0;
	v->ptoffset=0;
	v->rvol=0;
	v->lvol=0;
	v->frequency=0;
	v->bank=0;
	v->mode=0;
	v->sample_start=0;
	v->sample_end=0;
	v->sample_loop=0;
}

inline long ssC140::find_sample( long adrs, long bank)
{
	long adr;

	adrs = (bank << 16) + adrs;

	switch (m_type) {
	case TYPE_SYSTEM2:
		adr = ((adrs & 0x200000) >> 2) | (adrs & 0x7ffff);
		break;
	case TYPE_SYSTEM21:
		adr = ((adrs & 0x300000) >> 1) | (adrs & 0x7ffff);
		break;
	case TYPE_SYSTEM21_B:
		if (adrs & 0x40000) {
			adr = (adrs & 0x200000) ? ((0x300000 >> 1) | ((adrs & 0x100000) >> 2) | (adrs & 0x3ffff)) : ((0x100000 >> 1) | ((adrs & 0x100000) >> 2) | (adrs & 0x3ffff));
		} else {
			adr = (adrs & 0x200000) ? ((0x200000 >> 1) | ((adrs & 0x100000) >> 2) | (adrs & 0x3ffff)) : (((adrs & 0x100000) >> 2) | (adrs & 0x3ffff));
		}
		break;
	default:
		adr = 0;
		break;
	}

	return adr;
}


ssC140::ssC140(int _type)
{
	m_type = _type;
}

ssC140::~ssC140()
{
}

bool ssC140::Initialize(int _basefreq, BYTE *_pcmdata)
{
	ssSoundDriverManager *manager = ssSoundDriverManager::Instance();
	const ssConfig &config = manager->GetConfig();
	sample_rate = config.sampling_rate;

	baserate = _basefreq;
	pRom = _pcmdata;
	{
		int i;
		LONG segbase=0;
		for(i=0;i<8;i++)
		{
			pcmtbl[i]=segbase;	//segment base value
			segbase += 16<<i;
		}
	}
	memset(REG,0,0x200 );
	{
		int i;
		for(i=0;i<MAX_VOICE;i++) init_voice( &voi[i] );
	}

	for (int i = 0; i < MAX_PCM; i++) {
		InitTrackInfo(&info[i]);
		sprintf(info[i].name, "C140 #%d", i);
	}

	SetMask(0);

	return true;
}

void ssC140::Write(int _adr, BYTE _data)
{
	const int offset = _adr & 0x1ff;
	const BYTE data = _data;

	REG[offset]=data;

	if (offset < 0x180) {
		const int ch = offset>>4;
		switch (offset & 0x0f) {
		case 0x00:
		case 0x01:
			{
				const int b = offset & 0x1f0;
				const BYTE r = REG[b + 0];
				const BYTE l = REG[b + 1];
				BYTE vol = l > r ? l : r;
				if (vol > 127) vol = 127;
				info[ch].volume = vol;

				int pan = (r - l) / 16;
				if (pan < -3) {
					pan = -3;
				} else if (pan > 3) {
					pan = 3;
				}
				info[ch].pan = ssTrackInfo::PAN_CENTER + pan;
			}
			break;
		case 0x02:
		case 0x03:
			{
				const int b = offset & 0x1f0;
				int freq = (REG[b+2]<<8) + REG[b+3];
				info[ch].keycode = Hz2Kc(440 * freq / 0x1920);
			}
			break;
		case 0x05:
			if (data & 0x80) {
				info[ch].keyon = 0xff;
			} else {
				info[ch].keyon &= 0x80;
			}
			break;
		}
	}

	if( offset<0x180 )
	{
		VOICE *v = &voi[offset>>4];
		if( (offset&0xf)==0x5 )
		{
			if( data&0x80 )
			{
				const struct voice_registers *vreg = (struct voice_registers *) &REG[offset&0x1f0];
				v->key=1;
				v->ptoffset=0;
				v->pos=0;
				v->lastdt=0;
				v->prevdt=0;
				v->dltdt=0;
				v->bank = vreg->bank;
				v->mode = data;
				v->sample_loop = vreg->loop_msb*256 + vreg->loop_lsb;
				v->sample_start = vreg->start_msb*256 + vreg->start_lsb;
				v->sample_end = vreg->end_msb*256 + vreg->end_lsb;
			}
			else
			{
				v->key=0;
			}
		}
	}
}

BYTE ssC140::Read(int _adr)
{
	return REG[_adr&0x1ff];
}

int ssC140::GetBufferCount(void) const
{
	return 1;
}

WORD ssC140::GetBufferFlag(int _b) const
{
	//return ssSoundStream::F_STEREO;
	return ssSoundStream::F_STEREO_LONG;
}

int ssC140::GetTrackCount(void) const
{
	return MAX_PCM;
}

ssTrackInfo *ssC140::GetInfo(int _track)
{
	if (_track < MAX_PCM) {
		return &info[_track];
	}

	return NULL;
}

int ssC140::GetRegs(BYTE *_buffer, int _count, int _offset)
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


void ssC140::Update(SHORT **_buffer, DWORD _count)
{
	int length = _count;
	int *mixer_buffer = (int *)_buffer[0];


	int		i,j;

	LONG	rvol,lvol;
	LONG	dt;
	LONG	sdt;
	LONG	st,ed,sz;

	CHAR	*pSampleData;
	LONG	frequency,delta,offset,pos;
	LONG	cnt;
	LONG	lastdt,prevdt,dltdt;
	float	pbase=(float)baserate*2.0 / (float)sample_rate;

	//INT16	*lmix, *rmix;
	int	*mix;

	//if(length>sample_rate) length=sample_rate;

	/* zap the contents of the mixer buffer */
	//memset(mixer_buffer_left, 0, length * sizeof(INT16));
	//memset(mixer_buffer_right, 0, length * sizeof(INT16));
	memset(mixer_buffer, 0, length * sizeof(int) * 2);

	//--- audio update
	for( i=0;i<MAX_VOICE;i++ )
	{
		VOICE *v = &voi[i];
		const struct voice_registers *vreg = (struct voice_registers *)&REG[i*16];
		const mute = (m_mask & (1<<i)) != 0;

		if( v->key )
		{
			frequency= vreg->frequency_msb*256 + vreg->frequency_lsb;

			/* Abort voice if no frequency value set */
			if(frequency==0) continue;

			/* Delta =  frequency * ((8MHz/374)*2 / sample rate) */
			delta=(long)((float)frequency * pbase);

			/* Calculate left/right channel volumes */
			lvol=(vreg->volume_left*32)/MAX_VOICE; //32ch -> 24ch
			rvol=(vreg->volume_right*32)/MAX_VOICE;

			/* Set mixer buffer base pointers */
			//lmix = mixer_buffer_left;
			//rmix = mixer_buffer_right;
			mix = mixer_buffer;

			/* Retrieve sample start/end and calculate size */
			st=v->sample_start;
			ed=v->sample_end;
			sz=ed-st;

			/* Retrieve base pointer to the sample data */
			pSampleData=(char*)((unsigned long)pRom + find_sample(st,v->bank));

			/* Fetch back previous data pointers */
			offset=v->ptoffset;
			pos=v->pos;
			lastdt=v->lastdt;
			prevdt=v->prevdt;
			dltdt=v->dltdt;

			/* Switch on data type */
			if(v->mode&8)
			{
				//compressed PCM (maybe correct...)
				/* Loop for enough to fill sample buffer as requested */
				for(j=0;j<length;j++)
				{
					offset += delta;
					cnt = (offset>>16)&0x7fff;
					offset &= 0xffff;
					pos+=cnt;
					//for(;cnt>0;cnt--)
					{
						/* Check for the end of the sample */
						if(pos >= sz)
						{
							/* Check if its a looping sample, either stop or loop */
							if(v->mode&0x10)
							{
								pos = (v->sample_loop - st);
							}
							else
							{
								v->key=0;
								break;
							}
						}

						/* Read the chosen sample byte */
						dt=pSampleData[pos];

						/* decompress to 13bit range */		//2000.06.26 CAB
						sdt=dt>>3;				//signed
						if(sdt<0)	sdt = (sdt<<(dt&7)) - pcmtbl[dt&7];
						else		sdt = (sdt<<(dt&7)) + pcmtbl[dt&7];

						prevdt=lastdt;
						lastdt=sdt;
						dltdt=(lastdt - prevdt);
					}

					/* Caclulate the sample value */
					dt=((dltdt*offset)>>16)+prevdt;

					/* Write the data to the sample buffers */
					if (!mute) {
						//*lmix++ +=(dt*lvol)>>(5+5);
						//*rmix++ +=(dt*rvol)>>(5+5);
						*mix++ +=(dt*lvol)>>(5+5-3);
						*mix++ +=(dt*rvol)>>(5+5-3);
					}
				}
			}
			else
			{
				/* linear 8bit signed PCM */

				for(j=0;j<length;j++)
				{
					offset += delta;
					cnt = (offset>>16)&0x7fff;
					offset &= 0xffff;
					pos += cnt;
					/* Check for the end of the sample */
					if(pos >= sz)
					{
						/* Check if its a looping sample, either stop or loop */
						if( v->mode&0x10 )
						{
							pos = (v->sample_loop - st);
						}
						else
						{
							v->key=0;
							break;
						}
					}

					if( cnt )
					{
						prevdt=lastdt;
						lastdt=pSampleData[pos];
						dltdt=(lastdt - prevdt);
					}

					/* Caclulate the sample value */
					dt=((dltdt*offset)>>16)+prevdt;

					/* Write the data to the sample buffers */
					if (!mute) {
						//*lmix++ +=(dt*lvol)>>5;
						//*rmix++ +=(dt*rvol)>>5;
						*mix++ +=(dt*lvol)>>(5-3);
						*mix++ +=(dt*rvol)>>(5-3);
					}
				}
			}

			/* Save positional data for next callback */
			v->ptoffset=offset;
			v->pos=pos;
			v->lastdt=lastdt;
			v->prevdt=prevdt;
			v->dltdt=dltdt;
		}
	}

}
