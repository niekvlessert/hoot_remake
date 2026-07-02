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

class XtalsoftDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_BATTLEGORILLA,
		TYPE_XTALJBOX,
	};
	static XtalsoftDriver *Instance(int _type = TYPE_GENERIC);
private:
	static XtalsoftDriver *m_Instance;
protected:
	XtalsoftDriver(int _type = TYPE_GENERIC);
public:
	virtual ~XtalsoftDriver();

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
		BGM_SIZE = 0x1000,
		TONE_SIZE = 0x1000,
		PLAY_FLAG = 0x00,
		PLAY_CODE = 0x01,
	};

	int m_type;

	int m_bgmaddr;
	int m_bgmsize;
	int m_toneaddr;
	int m_tonesize;
	
	ssYM2608 *m_YM2608;
	ssMemoryDump *m_MemDump;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	bool bgm_flag[64];
	BYTE bgm[BGM_SIZE * 64];
	bool tone_flag[64];
	BYTE tone[TONE_SIZE * 64];
};

XtalsoftDriver *XtalsoftDriver::m_Instance = NULL;

XtalsoftDriver *XtalsoftDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new XtalsoftDriver(_type);
	}
	return m_Instance;
}

XtalsoftDriver::XtalsoftDriver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);
	m_MemDump = new ssMemoryDump();

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	for (int b = 0; b < 32; b++) {
		bgm_flag[b] = false;
		tone_flag[b] = false;
	}

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}
}

XtalsoftDriver::~XtalsoftDriver()
{
	delete m_YM2608;
	delete m_MemDump;

	m_Instance = NULL;
}

void XtalsoftDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE XtalsoftDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void XtalsoftDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	XtalsoftDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE XtalsoftDriver::sReadCtrl(WORD _adr)
{
	return XtalsoftDriver::m_Instance->ReadCtrl(_adr);
}

BYTE XtalsoftDriver::ReadPort(WORD _port)
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
	default:
		TRACE("%04x port-read(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, ioport[port]);
		break;
	}
	return ioport[port];
}

void XtalsoftDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x01:
		if (_data == 0xff) {
			ioport[PLAY_FLAG] = 0x00;
		} else if (bgm_flag[_data]) {
			memcpy(&ram[m_bgmaddr], &bgm[BGM_SIZE * _data], m_bgmsize);
			if (tone_flag[_data]) {
				memcpy(&ram[m_toneaddr], &tone[TONE_SIZE * _data], m_tonesize);
			}
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
	default:
		TRACE("%04x port-write(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE XtalsoftDriver::sReadPort(WORD _port)
{
	return XtalsoftDriver::m_Instance->ReadPort(_port);
}

void XtalsoftDriver::sWritePort(WORD _port, BYTE _data)
{
	XtalsoftDriver::m_Instance->WritePort(_port, _data);
}

void XtalsoftDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	Unlock();
}

bool XtalsoftDriver::Initialize(ssDriverConfig *_config)
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
			} else if (i->type == "tone") {
				tone_flag[i->offset] = true;
				zip.Load(i->filename, tone + i->offset * TONE_SIZE, TONE_SIZE);
			}
		}

		zip.Close();
	}

	switch (m_type) {
	case TYPE_BATTLEGORILLA:
		m_bgmaddr = 0x4400;
		m_bgmsize = 0x0700;
		m_toneaddr = 0;
		m_tonesize = 0;
		break;
	case TYPE_XTALJBOX:
		m_bgmaddr = 0x5000;
		m_bgmsize = 0x1000;
		m_toneaddr = 0x2b96;
		m_tonesize = 0x0400;
		break;
	default:
		m_bgmaddr = 0;
		m_bgmsize = 0;
		m_toneaddr = 0;
		m_tonesize = 0;
		break;
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);

	z80_add_read(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_add_write(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_reset();

	m_YM2608->Initialize(7987200, NULL, 0);
	m_YM2608->SetVolume(0, 0, 0x100);
	sdm->RegisterSoundChip(m_YM2608);

	m_MemDump->SetAddress(&ram[0x9e00]);
	//sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(1000);

	return true;
}

void XtalsoftDriver::Interrupt(int _id)
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

bool XtalsoftDriver::Play(DWORD _code)
{
	Lock();
	ioport[PLAY_FLAG] = 0x01;
	ioport[PLAY_CODE] = _code;
	Unlock();
	return true;
}

bool XtalsoftDriver::Stop()
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
		"xtalsoft",
		"battlegorilla",
		{"BATTLE GORILLA"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	{
		"xtalsoft",
		"xtaljbox",
		{"XTALSOFT DIGITAL JUKE BOX"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
			{"tone",	{"TONE data", "offset means TONE number"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "xtalsoft") {
		if (_config->driver_subtype == "battlegorilla") {
			return XtalsoftDriver::Instance(XtalsoftDriver::TYPE_BATTLEGORILLA);
		} else if (_config->driver_subtype == "xtaljbox") {
			return XtalsoftDriver::Instance(XtalsoftDriver::TYPE_XTALJBOX);
		}
	}
	return NULL;
}

static ssDriverRegister mucom88(string("xtalsoft"), CreateDriver, Description);
