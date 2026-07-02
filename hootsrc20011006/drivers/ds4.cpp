#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssAY8910.h"

#include "ssUnZip.h"

#include "raze/raze.h"

static BYTE ds4ipl [] = {
  0xed,0x56,0x31,0x80,0xf3,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0xf2,  /* 0000 */
  0x48,0xdb,0x00,0xb7,0x20,0x04,0xdb,0x02,0x18,0xf7,0xf3,0x3e,0x02,0x32,0x00,0x70,  /* 0010 */
  0x3c,0x32,0x00,0x78,0xdb,0x01,0x32,0x94,0xc0,0xcd,0x9d,0x72,0xfb,0x18,0xe2,0xff,  /* 0020 */
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf3,0xf5,0xc5,0xd5,0xe5,0xdd,0xe5,0xfd,  /* 0030 */
  0xe5,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0x36,0x72,0xfd,0xe1,0xdd,  /* 0040 */
  0xe1,0xe1,0xd1,0xc1,0xf1,0xfb,0xc9,
};

class Ds4Driver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static Ds4Driver *Instance(int _type = TYPE_GENERIC);
private:
	static Ds4Driver *m_Instance;
protected:
	Ds4Driver(int _type = TYPE_GENERIC);
public:
	virtual ~Ds4Driver();

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
		BGM_SIZE = 4 * 1024,
		PLAY_FLAG = 0x00,
		PLAY_CODE = 0x01,
		SKIP = 0x02,
	};

	int m_type;
	
	ssAY8910 *m_AY8910;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE rom[0x40000];
	BYTE ioport[0x100];
};

Ds4Driver *Ds4Driver::m_Instance = NULL;

Ds4Driver *Ds4Driver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new Ds4Driver(_type);
	}
	return m_Instance;
}

Ds4Driver::Ds4Driver(int _type)
{
	m_type = _type;

	m_AY8910 = new ssAY8910();
	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/60.0);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));
}

Ds4Driver::~Ds4Driver()
{
	if (m_AY8910) {
		delete m_AY8910;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	m_Instance = NULL;
}

void Ds4Driver::WriteCtrl(WORD _adr, BYTE _data)
{
	if (_adr == 0x7000) {
		z80_map_fetch(0x8000, 0x9fff, &rom[0x2000*_data]);
		z80_map_read(0x8000, 0x9fff, &rom[0x2000*_data]);
	} else if (_adr == 0x7800) {
		z80_map_fetch(0xa000, 0xbfff, &rom[0x2000*_data]);
		z80_map_read(0xa000, 0xbfff, &rom[0x2000*_data]);
	} else {
		TRACE("%04x : %04x : %02x\n", z80_get_reg(Z80_REG_PC), _adr, _data);
		ram[_adr] = _data;
	}
}

BYTE Ds4Driver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void Ds4Driver::sWriteCtrl(WORD _adr, BYTE _data)
{
	Ds4Driver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE Ds4Driver::sReadCtrl(WORD _adr)
{
	return Ds4Driver::m_Instance->ReadCtrl(_adr);
}

BYTE Ds4Driver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case PLAY_FLAG:
		{
			BYTE ret = ioport[PLAY_FLAG];
			ioport[PLAY_FLAG] = 0;
			return ret;
		}
	case SKIP:
		z80_skip_idle();
		return 0;
	case 0xa0:
	case 0xa1:
		return m_AY8910->Read();
		break;
	}
	return ioport[port];
}

void Ds4Driver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0xa0:
	case 0xa1:
		{
			TRACE("%02x : %02x\n", port, _data);
		}
		m_AY8910->Write(_port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE Ds4Driver::sReadPort(WORD _port)
{
	return Ds4Driver::m_Instance->ReadPort(_port);
}

void Ds4Driver::sWritePort(WORD _port, BYTE _data)
{
	Ds4Driver::m_Instance->WritePort(_port, _data);
}

void Ds4Driver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(3579545.0 * _second));
	Unlock();
}

bool Ds4Driver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, rom + i->offset, 0x40000);
			}
		}
		zip.Close();
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0x3fff, &ram[0x0000]);
	z80_map_fetch(0x4000, 0x7fff, &rom[0x0000]);
	z80_map_fetch(0x8000, 0x9fff, &rom[0x4000]);
	z80_map_fetch(0xa000, 0xbfff, &rom[0x6000]);
	z80_map_fetch(0xc000, 0xffff, &ram[0xc000]);

	z80_map_read(0x0000, 0x3fff, &ram[0x0000]);
	z80_map_read(0x4000, 0x7fff, &rom[0x0000]);
	z80_map_read(0x8000, 0x9fff, &rom[0x4000]);
	z80_map_read(0xa000, 0xbfff, &rom[0x6000]);
	z80_map_read(0xc000, 0xffff, &ram[0xc000]);

	z80_map_write(0x0000, 0x3fff, &ram[0x0000]);
	z80_add_write(0x7000, 0x7000, Z80_MAP_HANDLED, sWriteCtrl);
	z80_add_write(0x7800, 0x7800, Z80_MAP_HANDLED, sWriteCtrl);
	z80_map_write(0xc000, 0xffff, &ram[0xc000]);

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	memcpy(&ram[0x0000], ds4ipl, sizeof(ds4ipl));

	ram[0x93] = 0xc3;	// PSG Write
	ram[0x94] = 0x02;
	ram[0x95] = 0x11;
	ram[0x96] = 0xc3;
	ram[0x97] = 0x0e;
	ram[0x98] = 0x11;

	ram[0x1102] = 0xf3;	// PSG Write
	ram[0x1103] = 0xd3;
	ram[0x1104] = 0xa0;
	ram[0x1105] = 0xf5;
	ram[0x1106] = 0x7b;
	ram[0x1107] = 0xd3;
	ram[0x1108] = 0xa1;
	ram[0x1109] = 0xfb;
	ram[0x110a] = 0xf1;
	ram[0x110b] = 0xc9;

	ram[0x110e] = 0xd3;
	ram[0x110f] = 0xa0;
	ram[0x1110] = 0xdb;
	ram[0x1111] = 0xa2;
	ram[0x1112] = 0xc9;

	z80_reset();

	m_AY8910->Initialize(3579545 / 2);
	m_AY8910->SetVolume(0, 0, 0xa0);
	sdm->RegisterSoundChip(m_AY8910);

	z80_emulate(10000);

	ram[0xc042] = 0x02;
	ram[0xc043] = 0x03;
	ram[0xc095] = 0xff;
	ram[0xc098] = 0xbf;

	return true;
}

void Ds4Driver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case TIMER_INT:
		z80_raise_IRQ(0xff);
		z80_lower_IRQ();
		break;
	default:
		break;
	}
	Unlock();
}

bool Ds4Driver::Play(DWORD _code)
{
	Lock();
	if ((_code & 0x80) == 0) {
		ioport[PLAY_FLAG] = 0x01;
		ioport[PLAY_CODE] = _code;
	} else {
		ram[0xc095] = _code & 0x7f;
	}
	Unlock();
	return true;
}

bool Ds4Driver::Stop()
{
	Lock();

	ioport[PLAY_FLAG] = 0x00;
	z80_reset();

	m_AY8910->Write(0, 0x07);
	m_AY8910->Write(1, 0x3f);
	for (int i = 0x08; i < 0x10; i++) {
		m_AY8910->Write(0, i);
		m_AY8910->Write(1, 0x00);
	}

	z80_emulate(10000);

	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"msx",
		"ds4",
		{"DRAGON SLAYER 4 (MSX)"},
		{
			// Files
			{"code",	{"program code"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "msx") {
		if (_config->driver_subtype == "ds4") {
			return Ds4Driver::Instance(Ds4Driver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister ds4(string("msx"), CreateDriver, Description);
