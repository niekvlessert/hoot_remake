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

class AngelusDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_ANGELUS,
	};
	static AngelusDriver *Instance(int _type = TYPE_GENERIC);
private:
	static AngelusDriver *m_Instance;
protected:
	AngelusDriver(int _type = TYPE_GENERIC);
public:
	virtual ~AngelusDriver();

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
		VSYNC_INT,
	};
	enum {
		BGM_SIZE = 5 * 1024,
		PLAY_FLAG = 0x01,
		PLAY_CODE = 0x02,
		SKIP_PORT = 0x00,
		DATA_ADDR_H = 0x03,
		DATA_ADDR_L = 0x04,
	};

	int m_type;

	ssYM2608 *m_YM2608;
	ssMemoryDump *m_MemDump;

	int m_data_addr;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	BYTE bgm[BGM_SIZE * 64];
};

AngelusDriver *AngelusDriver::m_Instance = NULL;

AngelusDriver *AngelusDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new AngelusDriver(_type);
	}
	return m_Instance;
}

AngelusDriver::AngelusDriver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);
	m_MemDump = new ssMemoryDump();

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));
	ioport[0x08] = 0xff;
}

AngelusDriver::~AngelusDriver()
{
	if (m_MemDump) {
		delete m_MemDump;
	}
	if (m_YM2608) {
		delete m_YM2608;
	}
	m_Instance = NULL;
}

void AngelusDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE AngelusDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void AngelusDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	AngelusDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE AngelusDriver::sReadCtrl(WORD _adr)
{
	return AngelusDriver::m_Instance->ReadCtrl(_adr);
}

BYTE AngelusDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case SKIP_PORT:
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

void AngelusDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case PLAY_CODE:
		ioport[PLAY_FLAG] = 0;
		memcpy(&ram[m_data_addr], &bgm[BGM_SIZE * _data], 0xe000 - m_data_addr);
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

BYTE AngelusDriver::sReadPort(WORD _port)
{
	return AngelusDriver::m_Instance->ReadPort(_port);
}

void AngelusDriver::sWritePort(WORD _port, BYTE _data)
{
	AngelusDriver::m_Instance->WritePort(_port, _data);
}

void AngelusDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(8000000.0 * _second));
	//z80_emulate((int)(8000000.0));
	m_MemDump->SetWORD(0, z80_get_reg(Z80_REG_PC));
	Unlock();
}

bool AngelusDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	int opna = 0xff;

	opna = _config->GetOption("opna", opna);
	ioport[0x08] = opna ? 0xff : 0x00;

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x1000);
			} else if (i->type == "bgm") {
				zip.Load(i->filename, bgm + i->offset * BGM_SIZE, BGM_SIZE);
			}
		}

		zip.Close();
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);

	z80_add_read(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_add_write(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_reset();

	m_YM2608->Initialize(7987200, 0, 0);
	m_YM2608->SetVolume(0, 0, 0x100);
	m_YM2608->SetVolume(2, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2608);

	m_MemDump->SetAddress(&ram[0xf100]);
	//sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(500);

	return true;
}

void AngelusDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2608->TimerAOver();
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
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
	default:
		break;
	}
	Unlock();
}

bool AngelusDriver::Play(DWORD _code)
{
	Lock();

	ioport[PLAY_FLAG] = 1;
	ioport[PLAY_CODE] = _code;

	m_data_addr = _code >> 8;

	ioport[DATA_ADDR_H] = m_data_addr >> 8;
	ioport[DATA_ADDR_L] = m_data_addr;

	Unlock();
	return true;
}

bool AngelusDriver::Stop()
{
	Lock();

	ioport[PLAY_FLAG] = 1;
	ioport[PLAY_CODE] = 0;

	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"enix",
		"angelus",
		{"ANGELUS (PC-88)"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
		{
			// Options
			{"opna",		1,	{"if non-zero, OPNA version"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "enix") {
		if (_config->driver_subtype == "angelus") {
			return AngelusDriver::Instance(AngelusDriver::TYPE_ANGELUS);
		}
	}
	return NULL;
}

static ssDriverRegister angelus(string("enix"), CreateDriver, Description);
