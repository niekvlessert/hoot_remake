#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssAY8910.h"
#include "sound/ssSN76496.h"
#include "sound/ss051649.h"
#include "sound/ssYM2413.h"
#include "sound/ssMemoryDump.h"

#include "ssFile.h"
#include "ssUnZip.h"

#include "raze/raze.h"

static BYTE kssipl [] = {
//   0    1    2    3    4    5    6    7    8    9    a    b    c    d    e    f
  0xd7,0xd3,0xa0,0xf5,0x7b,0xd3,0xa1,0xf1,0xc9,0xd3,0xa0,0xdb,0xa2,0xc9,0xff,0xff,  /* 0000 */
  0xed,0x56,0x31,0x80,0xf3,0xf3,0xdb,0x00,0xcd,0x00,0x00,0xfb,0xdb,0x01,0x18,0xfb,  /* 0010 */
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,  /* 0020 */
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf3,0xcd,0x00,0x00,0xfb,0xc9,
};

class KssDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static KssDriver *Instance(int _type = TYPE_GENERIC);
private:
	static KssDriver *m_Instance;
protected:
	KssDriver(int _type = TYPE_GENERIC);
public:
	virtual ~KssDriver();

	bool Initialize(ssDriverConfig *_config);
	void Execute(double _second);
	void Interrupt(int _id);
	bool Play(DWORD _code);
	bool Stop();

private:
	static BYTE sReadCtrl(WORD _adr);
	static void sWriteCtrl(WORD _adr, BYTE _data);
	static BYTE sReadPort(WORD _port);
	static void sWritePort(WORD _port, BYTE _data);
private:
	void WriteCtrl(WORD _adr, BYTE _data);
	BYTE ReadCtrl(WORD _adr);
	void WritePort(WORD _port, BYTE _data);
	BYTE ReadPort(WORD _port);
private:
	enum {
		TIMER_IDLE = 0,
		TIMER_INT,
	};
	enum {
		INIT_ADR = 0x0019,
		INT_ADR = 0x003a,
		PLAY_CODE = 0x00,
		SKIP = 0x01,
	};
	enum {
		CHIP_FMPAC      = (1<<0),
		CHIP_SNG        = (1<<1),
		CHIP_GGSTEREO   = (1<<2),
		CHIP_SCCDISABLE = (1<<7),
	};

	int m_type;
	
	ssAY8910 *m_AY8910;
	ssSN76496 *m_SN76496;
	ss051649 *m_051649;
	ssYM2413 *m_YM2413;
	ssMemoryDump *m_MemDump;
	ssTimer *m_Timer;

	WORD m_loadadrs;
	WORD m_loadsize;
	WORD m_initadrs;
	WORD m_intadrs;
	BYTE m_bankofs;
	BYTE m_banknum;
	bool m_8kbank;
	BYTE m_chips;

	bool m_sccenable;

	bool m_playing;
	bool m_idle;

	BYTE ram[0x10000];
	BYTE rom[0x40000];
	BYTE *bank;
	BYTE ioport[0x100];
};

KssDriver *KssDriver::m_Instance = NULL;

KssDriver *KssDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new KssDriver(_type);
	}
	return m_Instance;
}

KssDriver::KssDriver(int _type)
{
	m_type = _type;

	m_AY8910 = NULL;
	m_SN76496 = NULL;
	m_YM2413 = NULL;
	m_051649 = NULL;
	m_MemDump = NULL;

	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/60.0);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));
	bank = 0;
}

KssDriver::~KssDriver()
{
	delete m_AY8910;
	delete m_SN76496;
	delete m_YM2413;
	delete m_051649;
	delete m_MemDump;
	delete m_Timer;
	if (bank) {
		delete[] bank;
	}
	m_Instance = NULL;
}

void KssDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	const WORD adr = _adr;
	const BYTE data = _data;

	if (m_8kbank) {
		if (adr == 0x9000) {
			const int bankno = data - m_bankofs;
			if (bankno < m_banknum) {
				z80_map_fetch(0x8000, 0x9fff, &bank[0x2000 * bankno]);
				z80_map_read(0x8000, 0x9fff, &bank[0x2000 * bankno]);
			} else {
				z80_map_fetch(0x8000, 0x9fff, &ram[0x8000]);
				z80_map_read(0x8000, 0x9fff, &ram[0x8000]);
			}
			return;
		} else if (adr == 0xb000) {
			const int bankno = data - m_bankofs;
			if (bankno < m_banknum) {
				z80_map_fetch(0xa000, 0xbfff, &bank[0x2000 * bankno]);
				z80_map_read(0xa000, 0xbfff, &bank[0x2000 * bankno]);
			} else {
				z80_map_fetch(0xa000, 0xbfff, &ram[0xa000]);
				z80_map_read(0xb000, 0xbfff, &ram[0xa000]);
			}
			return;
		}
	}
	if (m_sccenable) {
		if (adr >= 0x9800 && adr <= 0x988f) {
			if (m_051649) {
				m_051649->Write(adr, data);
			}
		} else {
			ram[adr] = data;
		}
	} else {
		ram[adr] = data;
	}
}

BYTE KssDriver::ReadCtrl(WORD _adr)
{
	const WORD adr = _adr;

	if ((adr & 0xff00) == 0x9800) {
		if (m_051649) {
			return m_051649->Read(adr);
		}
	}

	return ram[adr];
}

void KssDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	KssDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE KssDriver::sReadCtrl(WORD _adr)
{
	return KssDriver::m_Instance->ReadCtrl(_adr);
}

BYTE KssDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case SKIP:
		m_idle = true;
		z80_skip_idle();
		return 0;
	case 0xa0:
	case 0xa1:
	case 0xa2:
		if (m_AY8910) {
			return m_AY8910->Read();
		}
		break;
	}
	return ioport[port];
}

void KssDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x7c:
	case 0x7d:
		if (m_YM2413) {
			m_YM2413->Write(_port, _data);
		}
		break;
	case 0x7e:
	case 0x7f:
		if (m_SN76496) {
			m_SN76496->Write(_data);
		}
		break;
	case 0xa0:
	case 0xa1:
		if (m_AY8910) {
			m_AY8910->Write(_port, _data);
		}
		break;
	case 0xc0:
	case 0xc1:
		if (m_YM2413) {
			m_YM2413->Write(_port, _data);
		}
		break;
	case 0xf0:
	case 0xf1:
		if (m_YM2413) {
			m_YM2413->Write(_port, _data);
		}
		break;
	case 0xfe:
		// Bank Change
		{
			const int bankno = _data - m_bankofs;
			if (bankno < 0 || bankno >= m_banknum) {
				z80_map_fetch(0x8000, 0xbfff, &ram[0x8000]);
				z80_map_read(0x8000, 0xbfff, &ram[0x8000]);
			} else {
				z80_map_fetch(0x8000, 0xbfff, &bank[0x4000 * bankno]);
				z80_map_read(0x8000, 0xbfff, &bank[0x4000 * bankno]);
			}
		}
		break;
	}
	ioport[port] = _data;
}

BYTE KssDriver::sReadPort(WORD _port)
{
	return KssDriver::m_Instance->ReadPort(_port);
}

void KssDriver::sWritePort(WORD _port, BYTE _data)
{
	KssDriver::m_Instance->WritePort(_port, _data);
}

void KssDriver::Execute(double _second)
{
	Lock();

	if (m_playing) {
		z80_emulate((int)(3579545.0 * _second));
	}
	m_MemDump->SetWORD(0, z80_get_reg(Z80_REG_PC));

	Unlock();
}

static WORD GetWORD(BYTE *p)
{
	WORD ret = (p[1]<<8) + p[0];
	return ret;
}

bool KssDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	int ay_mix      = 0x100;
	int sn_mix      = 0x100;
	int fmpac_mix   = 0xcc;
	int fmunit_mix  = 0xcc;
	int scc_mix     = 0xcc;

	ay_mix = _config->GetOption("ay_mix", ay_mix);
	sn_mix = _config->GetOption("sn_mix", sn_mix);
	fmpac_mix = _config->GetOption("fmpac_mix", fmpac_mix);
	fmunit_mix = _config->GetOption("fmunit_mix", fmunit_mix);
	scc_mix = _config->GetOption("scc_mix", scc_mix);

	if (_config->IsSingleFile()) {
		ssFile file;
		if (file.Open(_config->archive)) {
			file.Read(rom, sizeof(rom));
		}
		for (DWORD i = 0; i < 0x100; i++) {
			char title[32];
			sprintf(title, "SOUND CODE %02x", i);
			_config->AddTitle(i, string(title));
		}
	} else {
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, rom , 0x40000);
			}
		}
		zip.Close();
	}

	{
		m_loadadrs = GetWORD(&rom[0x0004]);
		m_loadsize = GetWORD(&rom[0x0006]);
		m_initadrs = GetWORD(&rom[0x0008]);
		m_intadrs  = GetWORD(&rom[0x000a]);
		m_bankofs  = rom[0x0c];
		m_banknum  = rom[0x0d] & 0x7f;
		m_8kbank   = ((rom[0x0d] & 0x80) != 0);
		m_chips    = rom[0x0f];
		m_sccenable = (!(m_chips & CHIP_SCCDISABLE))
			&& ((m_chips & (CHIP_SNG|CHIP_GGSTEREO)) != CHIP_GGSTEREO);

		if (m_banknum) {
			bank = new BYTE[0x4000 * m_banknum];
		}

		memcpy(bank, &rom[0x0010 + m_loadsize], 0x4000 * m_banknum);
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);
	z80_map_read(0x0000, 0xffff, &ram[0x0000]);

	z80_map_write(0x0000, 0x7fff, &ram[0x0000]);
	z80_add_write(0x8000, 0xbfff, Z80_MAP_HANDLED, sWriteCtrl);
	z80_map_write(0xc000, 0xffff, &ram[0xc000]);

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_end_memmap();

	memset(&ram[0x0000], 0xc9, 0x4000);
	memset(&ram[0x4000], 0x00, 0xc000);
	memmove(&ram[m_loadadrs], &rom[0x0010], m_loadsize);
	memcpy(&ram[0x0000], kssipl, sizeof(kssipl));

	ram[INIT_ADR  ] = m_initadrs;
	ram[INIT_ADR+1] = m_initadrs >> 8;
	ram[INT_ADR  ] = m_intadrs;
	ram[INT_ADR+1] = m_intadrs >> 8;

	ram[0x93] = 0xc3;	// PSG Write
	ram[0x94] = 0x01;
	ram[0x95] = 0x00;
	ram[0x96] = 0xc3;	// PSG Read
	ram[0x97] = 0x09;
	ram[0x98] = 0x00;

	z80_reset();

	if (m_chips & CHIP_SNG) {
		m_SN76496 = new ssSN76496();
		//m_SN76496->Initialize(4194304);
		m_SN76496->Initialize(3579545);
		m_SN76496->SetVolume(0, 0, sn_mix);
		sdm->RegisterSoundChip(m_SN76496);
	} else {
		m_AY8910 = new ssAY8910();
		m_AY8910->Initialize(3579545 / 2);
		m_AY8910->SetVolume(0, 0, ay_mix);
		sdm->RegisterSoundChip(m_AY8910);
	}

	if (m_chips & CHIP_FMPAC) {
		m_YM2413 = new ssYM2413();
		if (m_chips & CHIP_SNG) {
			//m_YM2413->Initialize(8000000);
			m_YM2413->Initialize(3579545);
			m_YM2413->SetVolume(0, 0, fmunit_mix);
		} else {
			m_YM2413->Initialize(3579545);
			m_YM2413->SetVolume(0, 0, fmpac_mix);
		}
		sdm->RegisterSoundChip(m_YM2413);
	}

	if (m_sccenable) {
		m_051649 = new ss051649();
		m_051649->Initialize(3579545 / 2);
		m_051649->SetVolume(0, 0, scc_mix);
		sdm->RegisterSoundChip(m_051649);
	}

	m_MemDump = new ssMemoryDump();
	m_MemDump->SetAddress(&ram[0]);
	//sdm->RegisterSoundChip(m_MemDump);

	m_playing = false;
	m_idle = false;

	return true;
}

void KssDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case TIMER_INT:
		if (m_playing) {
			z80_raise_IRQ(0xff);
			z80_lower_IRQ();
		}
		break;
	default:
		break;
	}
	Unlock();
}

bool KssDriver::Play(DWORD _code)
{
	Lock();

	memset(&ram[0x0000], 0xc9, 0x4000);
	memset(&ram[0x4000], 0x00, 0xc000);
	memmove(&ram[m_loadadrs], &rom[0x0010], m_loadsize);
	memcpy(&ram[0x0000], kssipl, sizeof(kssipl));

	ram[INIT_ADR  ] = m_initadrs;
	ram[INIT_ADR+1] = m_initadrs >> 8;
	ram[INT_ADR  ] = m_intadrs;
	ram[INT_ADR+1] = m_intadrs >> 8;

	ram[0x93] = 0xc3;	// PSG Write
	ram[0x94] = 0x01;
	ram[0x95] = 0x00;
	ram[0x96] = 0xc3;
	ram[0x97] = 0x09;
	ram[0x98] = 0x00;

	ioport[PLAY_CODE] = _code;
	WritePort(0xfe, 0);

	z80_reset();

	m_idle = false;
	do {
		z80_emulate(100000);
	} while (!m_idle);

	m_playing = true;

	Unlock();
	return true;
}

bool KssDriver::Stop()
{
	Lock();

	ioport[PLAY_CODE] = 0x00;

	m_AY8910->Write(0, 0x07);
	m_AY8910->Write(1, 0x3f);
	for (int i = 0x08; i < 0x10; i++) {
		m_AY8910->Write(0, i);
		m_AY8910->Write(1, 0x00);
	}
	m_051649->InitSccWork();

	m_playing = false;

	Unlock();
	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"msx",
		"kss",
		{"MSX (KSS format)", "SEGA MASTER SYSTEM (KSS format)", "SEGA GAME GEAR (KSS format)"},
		{
			// Files
			{"code",	{"KSS data"}},
		},
		{
			// Options
			{"ay_mix",		0x100,		{"AY-3-8910 mixing level"}},
			{"sn_mix",		0x100,		{"SN76496 mixing level"}},
			{"fmpac_mix",	0xcc,		{"FMPAC mixing level"}},
			{"fmunit_mix",	0xcc,		{"FMUNIT mixing level"}},
			{"scc_mix",		0xcc,		{"051649 mixing level"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "msx") {
		if (_config->driver_subtype == "kss") {
			return KssDriver::Instance(KssDriver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister kss(string("msx"), CreateDriver, Description);
