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

class RevolterDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_REVOLTER,
	};
	static RevolterDriver *Instance(int _type = TYPE_GENERIC);
private:
	static RevolterDriver *m_Instance;
protected:
	RevolterDriver(int _type = TYPE_GENERIC);
public:
	virtual ~RevolterDriver();

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
		BGM_SIZE = 0x9e00 - 0x8600,
		PLAY_FLAG = 0x00,
		PLAY_CODE = 0x01,
	};

	int m_type;
	
	ssYM2608 *m_YM2608;
	ssMemoryDump *m_MemDump;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	BYTE pcmdata[0x40000];

	bool bgm_flag[32];
	BYTE bgm[BGM_SIZE * 32];
};

RevolterDriver *RevolterDriver::m_Instance = NULL;

RevolterDriver *RevolterDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new RevolterDriver(_type);
	}
	return m_Instance;
}

RevolterDriver::RevolterDriver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);
	m_MemDump = new ssMemoryDump();

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	memset(pcmdata, 0x80, sizeof(pcmdata));

	for (int b = 0; b < 32; b++) {
		bgm_flag[b] = false;
	}

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}
}

RevolterDriver::~RevolterDriver()
{
	delete m_YM2608;
	delete m_MemDump;

	m_Instance = NULL;
}

void RevolterDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE RevolterDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void RevolterDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	RevolterDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE RevolterDriver::sReadCtrl(WORD _adr)
{
	return RevolterDriver::m_Instance->ReadCtrl(_adr);
}

BYTE RevolterDriver::ReadPort(WORD _port)
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
#if 0
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
#endif
	default:
		TRACE("%04x port-read(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, ioport[port]);
		break;
	}
	return ioport[port];
}

void RevolterDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x01:
		if (_data == 0xff) {
			ioport[PLAY_FLAG] = 0x00;
		} else if (bgm_flag[_data]) {
			memcpy(&ram[0x8600], &bgm[BGM_SIZE * _data], BGM_SIZE);
			ioport[PLAY_FLAG] = 0x00;
			TRACE("BGM #%02x Loaded\n", _data);
		}
		break;
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		m_YM2608->Write(_port, _data);
		TRACE("%04x OPNA(%02x) : %02x\n", 
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
#if 0
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
#endif
	default:
		TRACE("%04x port-write(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE RevolterDriver::sReadPort(WORD _port)
{
	return RevolterDriver::m_Instance->ReadPort(_port);
}

void RevolterDriver::sWritePort(WORD _port, BYTE _data)
{
	RevolterDriver::m_Instance->WritePort(_port, _data);
}

void RevolterDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	Unlock();
}

bool RevolterDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++)
		{
			if (i->type == "code")
			{
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

	m_YM2608->Initialize(7987200, pcmdata, sizeof(pcmdata));
	m_YM2608->SetVolume(0, 0, 0x100);
	m_YM2608->SetVolume(2, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2608);

	m_MemDump->SetAddress(&ram[0x9e00]);
	//sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(1000);

	return true;
}

void RevolterDriver::Interrupt(int _id)
{
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
//		z80_raise_IRQ(4);
//		z80_lower_IRQ();
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	default:
		break;
	}
	Unlock();
}

bool RevolterDriver::Play(DWORD _code)
{
	Lock();
	ioport[PLAY_FLAG] = 0x01;
	ioport[PLAY_CODE] = _code;
	Unlock();
	return true;
}

bool RevolterDriver::Stop()
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
		"trpscr",
		"revolter",
		{"REVOLTER"},
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
	if (_config->driver_majortype == "trpscr") {
		if (_config->driver_subtype == "revolter") {
			return RevolterDriver::Instance(RevolterDriver::TYPE_REVOLTER);
		}
	}
	return NULL;
}

static ssDriverRegister mucom88(string("trpscr"), CreateDriver, Description);
