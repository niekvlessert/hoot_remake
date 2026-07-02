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

class StarshipDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static StarshipDriver *Instance(int _type = TYPE_GENERIC);
private:
	static StarshipDriver *m_Instance;
protected:
	StarshipDriver(int _type = TYPE_GENERIC);
public:
	virtual ~StarshipDriver();

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
	};

	int m_type;

	ssYM2203 *m_YM2203;
	ssMemoryDump *m_MemDump;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	BYTE bgm[BGM_SIZE * 16];
};

StarshipDriver *StarshipDriver::m_Instance = NULL;

StarshipDriver *StarshipDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new StarshipDriver(_type);
	}
	return m_Instance;
}

StarshipDriver::StarshipDriver(int _type)
{
	m_type = _type;

	m_YM2203 = new ssYM2203(FM_TIMER_A, FM_TIMER_B);
	m_MemDump = new ssMemoryDump;

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}

	memset(bgm, 0, sizeof(bgm));
}

StarshipDriver::~StarshipDriver()
{
	if (m_MemDump) {
		delete m_MemDump;
	}
	if (m_YM2203) {
		delete m_YM2203;
	}
	m_Instance = NULL;
}

void StarshipDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE StarshipDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void StarshipDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	StarshipDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE StarshipDriver::sReadCtrl(WORD _adr)
{
	return StarshipDriver::m_Instance->ReadCtrl(_adr);
}

BYTE StarshipDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
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

void StarshipDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		z80_skip_idle();
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

BYTE StarshipDriver::sReadPort(WORD _port)
{
	return StarshipDriver::m_Instance->ReadPort(_port);
}

void StarshipDriver::sWritePort(WORD _port, BYTE _data)
{
	StarshipDriver::m_Instance->WritePort(_port, _data);
}

void StarshipDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(8000000.0 * _second));
	m_MemDump->SetWORD(0, z80_get_reg(Z80_REG_PC));
	Unlock();
}

bool StarshipDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x8000);
			} else if (i->type == "bgm") {
				zip.Load(i->filename, bgm + i->offset * BGM_SIZE, BGM_SIZE);
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

	z80_set_reg(Z80_REG_PC, 0xe000);

	m_YM2203->Initialize(3993600);
	m_YM2203->SetVolume(0, 0, 0x100);
	sdm->RegisterSoundChip(m_YM2203);

	m_MemDump->SetAddress(&ram[0x0000]);
	sdm->RegisterSoundChip(m_MemDump);

	z80_emulate(5000);

	return true;
}

void StarshipDriver::Interrupt(int _id)
{
	static x = 0;
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2203->TimerAOver();
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
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
	default:
		break;
	}
	Unlock();
}

bool StarshipDriver::Play(DWORD _code)
{
	Lock();

	int i;

	m_YM2203->Write(0, 0x07);
	m_YM2203->Write(1, 0x3f);
	for (i = 0; i < 3; i++) {

		m_YM2203->Write(0, 0x08 + i);
		m_YM2203->Write(1, 0x00);

		m_YM2203->Write(0, 0x80 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x84 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x88 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x8c + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x28);
		m_YM2203->Write(1, i);
	}

	memcpy(&ram[0x7000], &bgm[BGM_SIZE * _code], 0x1000);

	z80_reset();
	z80_set_reg(Z80_REG_PC, 0xe003);
	z80_emulate(5000);

	Unlock();
	return true;
}

bool StarshipDriver::Stop()
{
	Lock();

	int i;

	m_YM2203->Write(0, 0x07);
	m_YM2203->Write(1, 0x3f);
	for (i = 0; i < 3; i++) {

		m_YM2203->Write(0, 0x08 + i);
		m_YM2203->Write(1, 0x00);

		m_YM2203->Write(0, 0x80 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x84 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x88 + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x8c + i);
		m_YM2203->Write(1, 0xff);
		m_YM2203->Write(0, 0x28);
		m_YM2203->Write(1, i);
	}

	z80_reset();
	z80_set_reg(Z80_REG_PC, 0xe000);

	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"scaptrust",
		"starship",
		{"STAR/SHIP RENDEZVOUS"},
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
	if (_config->driver_majortype == "scaptrust") {
		if (_config->driver_subtype == "starship") {
			return StarshipDriver::Instance(StarshipDriver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister starship(string("scaptrust"), CreateDriver, Description);
