#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2203.h"
#include "sound/ssMemoryDump.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class ZavasDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_ZAVAS,
	};
	static ZavasDriver *Instance(int _type = TYPE_GENERIC);
private:
	static ZavasDriver *m_Instance;
protected:
	ZavasDriver(int _type = TYPE_GENERIC);
public:
	virtual ~ZavasDriver();

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
		BGM_SIZE = 0x400 * 9,
		PLAY_FLAG = 0x00,
		PLAY_CODE = 0x01,
	};

	int m_type;

	ssYM2203 *m_YM2203;
	ssMemoryDump *m_MemDump;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	bool bgm_flag[64];
	BYTE bgm[BGM_SIZE * 64];
};

ZavasDriver *ZavasDriver::m_Instance = NULL;

ZavasDriver *ZavasDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new ZavasDriver(_type);
	}
	return m_Instance;
}

ZavasDriver::ZavasDriver(int _type)
{
	m_type = _type;

	m_YM2203 = new ssYM2203(FM_TIMER_A, FM_TIMER_B);
	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1./60.);
	m_MemDump = new ssMemoryDump();

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	for (int b = 0; b < 32; b++) {
		bgm_flag[b] = false;
	}

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}
}

ZavasDriver::~ZavasDriver()
{
	delete m_YM2203;
	delete m_MemDump;
	delete m_Timer;

	m_Instance = NULL;
}

void ZavasDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE ZavasDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void ZavasDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	ZavasDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE ZavasDriver::sReadCtrl(WORD _adr)
{
	return ZavasDriver::m_Instance->ReadCtrl(_adr);
}

BYTE ZavasDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		z80_skip_idle();
		break;
	case 0x44:
	case 0x45:
		return m_YM2203->Read(_port);
		break;
	default:
		TRACE("%04x port-read(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, ioport[port]);
		break;
	}
	return ioport[port];
}

void ZavasDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x01:
		if (_data == 0xff) {
			ioport[PLAY_FLAG] = 0x00;
		} else if (bgm_flag[_data]) {
			memcpy(&ram[0x9000], &bgm[BGM_SIZE * _data], BGM_SIZE);
			ioport[PLAY_FLAG] = 0x00;
			TRACE("BGM #%02x Loaded\n", _data);
		}
		break;
	case 0x44:
	case 0x45:
		m_YM2203->Write(_port, _data);
		TRACE("%04x OPNA(%02x) : %02x\n", 
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	default:
		TRACE("%04x port-write(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE ZavasDriver::sReadPort(WORD _port)
{
	return ZavasDriver::m_Instance->ReadPort(_port);
}

void ZavasDriver::sWritePort(WORD _port, BYTE _data)
{
	ZavasDriver::m_Instance->WritePort(_port, _data);
}

void ZavasDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(8000000.0 * _second));
	Unlock();
}

bool ZavasDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++)
		{
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x4000);
			} else if (i->type == "bgm") {
				bgm_flag[i->offset] = true;
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

	m_YM2203->Initialize(3993600);
	m_YM2203->SetVolume(0, 0, 0x100);
	sdm->RegisterSoundChip(m_YM2203);

	m_MemDump->SetAddress(&ram[0x9e00]);
	//sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(1000);

	return true;
}

void ZavasDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2203->TimerAOver();
#if 1
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
#endif
		//TRACE("A Tick:%d\n", GetTickCount());
		break;
	case FM_TIMER_B:
		m_YM2203->TimerBOver();
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	case TIMER_INT:
		z80_raise_IRQ(2);
		z80_lower_IRQ();
		break;
	default:
		break;
	}
	Unlock();
}

bool ZavasDriver::Play(DWORD _code)
{
	Lock();
	ioport[PLAY_FLAG] = 0x01;
	ioport[PLAY_CODE] = _code;
	Unlock();
	return true;
}

bool ZavasDriver::Stop()
{
	Lock();

	ioport[PLAY_FLAG] = 0x02;

	Unlock();
	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"glodia",
		"zavas",
		{"ZAVAS"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "glodia") {
		if (_config->driver_subtype == "zavas") {
			return ZavasDriver::Instance(ZavasDriver::TYPE_ZAVAS);
		}
	}
	return NULL;
}

static ssDriverRegister glodia(string("glodia"), CreateDriver, Description);
