#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2608.h"
#include "sound/ssMemoryDump.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class StarcruiserDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static StarcruiserDriver *Instance(int _type = TYPE_GENERIC);
private:
	static StarcruiserDriver *m_Instance;
protected:
	StarcruiserDriver(int _type = TYPE_GENERIC);
public:
	virtual ~StarcruiserDriver();

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
		FM_TIMER_A,
		FM_TIMER_B,
		TIMER_INT,
	};
	enum {
		BGM_SIZE = 0x1200,
	};

	int m_type;

	ssYM2608 *m_YM2608;
	ssMemoryDump *m_MemDump;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	BYTE bgm1[0x10000];
	BYTE bgm2[0x10000];
};

StarcruiserDriver *StarcruiserDriver::m_Instance = NULL;

StarcruiserDriver *StarcruiserDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new StarcruiserDriver(_type);
	}
	return m_Instance;
}

StarcruiserDriver::StarcruiserDriver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);
	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/600.0);
	//m_Timer->SetInterval(1.0/60.0);
	m_MemDump = new ssMemoryDump;

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}

	memset(bgm1, 0, sizeof(bgm1));
	memset(bgm2, 0, sizeof(bgm2));
}

StarcruiserDriver::~StarcruiserDriver()
{
	if (m_MemDump) {
		delete m_MemDump;
	}
	if (m_YM2608) {
		delete m_YM2608;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	m_Instance = NULL;
}

void StarcruiserDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE StarcruiserDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void StarcruiserDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	StarcruiserDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE StarcruiserDriver::sReadCtrl(WORD _adr)
{
	return StarcruiserDriver::m_Instance->ReadCtrl(_adr);
}

BYTE StarcruiserDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		z80_skip_idle();
		break;
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		return m_YM2608->Read(_port);
		break;
	case 0xa8:
		return m_YM2608->Read(0);
	case 0xa9:
		return m_YM2608->Read(1);
	case 0xaa:
		return ioport[0x32];
	case 0xac:
		return m_YM2608->Read(2);
	case 0xad:
		return m_YM2608->Read(3);
	default:
		TRACE("%04x port-read(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, ioport[port]);
		break;
	}
	return ioport[port];
}

void StarcruiserDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		z80_skip_idle();
		break;
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		m_YM2608->Write(_port, _data);
		TRACE("%04x OPNA(%02x) : %02x\n", 
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	case 0xa8:
		m_YM2608->Write(0, _data);
		break;
	case 0xa9:
		m_YM2608->Write(1, _data);
		break;
	case 0xaa:
		ioport[0x32] = _data;
		break;
	case 0xac:
		m_YM2608->Write(2, _data);
		break;
	case 0xad:
		m_YM2608->Write(3, _data);
		break;
	default:
		TRACE("%04x port-write(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE StarcruiserDriver::sReadPort(WORD _port)
{
	return StarcruiserDriver::m_Instance->ReadPort(_port);
}

void StarcruiserDriver::sWritePort(WORD _port, BYTE _data)
{
	StarcruiserDriver::m_Instance->WritePort(_port, _data);
}

void StarcruiserDriver::Execute(double _second)
{
	Lock();
	//z80_emulate((int)(8000000.0 * _second));
	z80_emulate((int)(8000000.0));
	m_MemDump->SetWORD(0, z80_get_reg(Z80_REG_PC));
	Unlock();
}

bool StarcruiserDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x8000);
			} else if (i->type == "bgm1") {
				zip.Load(i->filename, bgm1 + i->offset, 0x10000);
			} else if (i->type == "bgm2") {
				zip.Load(i->filename, bgm2 + i->offset, 0x10000);
			}
		}

		zip.Close();
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);
	z80_map_read(0x0000, 0xffff, &ram[0x0000]);
	z80_map_write(0x0000, 0xffff, &ram[0x0000]);

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_end_memmap();

	z80_reset();

	z80_set_reg(Z80_REG_PC, 0x8000);

	m_YM2608->Initialize(7987200, 0, 0);
	m_YM2608->SetVolume(0, 0, 0x100);
	sdm->RegisterSoundChip(m_YM2608);

	sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(5000);

	return true;
}

void StarcruiserDriver::Interrupt(int _id)
{
	static x = 0;
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2608->TimerAOver();
#if 1
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
#endif
		//TRACE("A Tick:%d\n", GetTickCount());
		break;
	case FM_TIMER_B:
		m_YM2608->TimerBOver();
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	case TIMER_INT:
		z80_raise_IRQ(4);
		z80_lower_IRQ();
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	default:
		break;
	}
	Unlock();
}

bool StarcruiserDriver::Play(DWORD _code)
{
	Lock();

	int i;

	m_YM2608->Write(0, 0x29);
	m_YM2608->Write(1, 0x82);

	m_YM2608->Write(0, 0x07);
	m_YM2608->Write(1, 0x3f);
	for (i = 0; i < 3; i++) {
		m_YM2608->Write(0, 0x08 + i);
		m_YM2608->Write(1, 0x00);

		m_YM2608->Write(0, 0x80 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x84 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x88 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x8c + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x28);
		m_YM2608->Write(1, i);

		m_YM2608->Write(2, 0x80 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x84 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x88 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x8c + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(0, 0x28);
		m_YM2608->Write(1, i | 0x04);
	}

	m_YM2608->Write(2, 0x10);
	m_YM2608->Write(3, 0xbf);
	m_YM2608->Write(2, 0x00);
	m_YM2608->Write(3, 0x00);

	int offset, size;
	BYTE *data;

	if ((_code >> 8) == 0) {
		data = bgm1;
		ram[0x0003] = 0;
	} else {
		data = bgm2;
		ram[0x0003] = 1;
	}

	int num = _code & 0xff;

	offset = (data[num*4+1]<<8) + data[num*4];
	size = (data[num*4+3]<<8) + data[num*4+2];

	memcpy(&ram[0xc000], &data[offset], size);
	ram[0x00d4] = 0x00;
	ram[0x00d5] = 0xc0;

	z80_reset();
	z80_set_reg(Z80_REG_PC, 0x8003);

	Unlock();
	return true;
}

bool StarcruiserDriver::Stop()
{
	Lock();

	int i;

	m_YM2608->Write(0, 0x07);
	m_YM2608->Write(1, 0x3f);
	for (i = 0; i < 3; i++) {
		m_YM2608->Write(0, 0x08 + i);
		m_YM2608->Write(1, 0x00);

		m_YM2608->Write(0, 0x80 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x84 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x88 + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0x8c + i);
		m_YM2608->Write(1, 0xff);
		m_YM2608->Write(0, 0xb4 + i);
		m_YM2608->Write(1, 0xc0);
		m_YM2608->Write(0, 0x28);
		m_YM2608->Write(1, i);

		m_YM2608->Write(2, 0x80 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x84 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x88 + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0x8c + i);
		m_YM2608->Write(3, 0xff);
		m_YM2608->Write(2, 0xb4 + i);
		m_YM2608->Write(3, 0xc0);
		m_YM2608->Write(0, 0x28);
		m_YM2608->Write(1, i | 0x04);
	}

	m_YM2608->Write(2, 0x10);
	m_YM2608->Write(3, 0xbf);
	m_YM2608->Write(2, 0x00);
	m_YM2608->Write(3, 0x00);

	z80_reset();
	z80_set_reg(Z80_REG_PC, 0x8000);

	Unlock();
	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"arsys",
		"starcruiser",
		{"STAR CRUISER"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm1",		{"BGM data (OPN)"}},
			{"bgm2",		{"BGM data (OPNA)"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "arsys") {
		if (_config->driver_subtype == "starcruiser") {
			return StarcruiserDriver::Instance(StarcruiserDriver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister starcruiser(string("arsys"), CreateDriver, Description);
