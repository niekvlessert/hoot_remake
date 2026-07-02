#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2151.h"
#include "sound/ssAY8910.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class MucomX1Driver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_PSGONLY,
	};
	static MucomX1Driver *Instance(int _type = TYPE_GENERIC);
private:
	static MucomX1Driver *m_Instance;
protected:
	MucomX1Driver(int _type = TYPE_GENERIC);
public:
	virtual ~MucomX1Driver();

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
		VSYNC_INT,
	};
	enum {
		BGM_SIZE = 8 * 1024,
		PLAY_FLAG = 0xc010,
		PLAY_CODE = 0xc011,
		LOAD_FLAG = 0xc012,
	};

	int m_type;
	
	ssYM2151 *m_YM2151;
	ssAY8910 *m_AY8910;
	ssTimer *m_Timer;
	ssTimer *m_TimerCTC3;
	ssTimer *m_TimerVsync;

	BYTE ram[0x10000];
	BYTE ioport[0x10000];

	bool bgm_flag[128];
	BYTE bgm[BGM_SIZE * 128];
};

MucomX1Driver *MucomX1Driver::m_Instance = NULL;

MucomX1Driver *MucomX1Driver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new MucomX1Driver(_type);
	}
	return m_Instance;
}

MucomX1Driver::MucomX1Driver(int _type)
{
	m_type = _type;

	m_YM2151 = new ssYM2151(FM_TIMER_A, FM_TIMER_B);
	m_AY8910 = new ssAY8910();
	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/4000000.0 * 256.0 * 18.0);
	m_TimerCTC3 = new ssTimer(TIMER_INT);
	m_TimerCTC3->SetInterval(1.0/4000000.0 * 256.0 * 18 * 15);
	m_TimerVsync = new ssTimer(VSYNC_INT);
	m_TimerVsync->SetInterval(1.0/60.0);
	//m_Timer->SetInterval(1.0/60.0);

	for (int b = 0; b < 128; b++) {
		bgm_flag[b] = false;
	}
}

MucomX1Driver::~MucomX1Driver()
{
	if (m_YM2151) {
		delete m_YM2151;
	}
	if (m_AY8910) {
		delete m_AY8910;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	if (m_TimerCTC3) {
		delete m_TimerCTC3;
	}
	if (m_TimerVsync) {
		delete m_TimerVsync;
	}
	m_Instance = NULL;
}

void MucomX1Driver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE MucomX1Driver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void MucomX1Driver::sWriteCtrl(WORD _adr, BYTE _data)
{
	MucomX1Driver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE MucomX1Driver::sReadCtrl(WORD _adr)
{
	return MucomX1Driver::m_Instance->ReadCtrl(_adr);
}

BYTE MucomX1Driver::ReadPort(WORD _port)
{
	switch (_port) {
	case 0x0700:
	case 0x0701:
		return m_YM2151->Read(_port);
		break;
	case 0x1a01:
		{
			TRACE("%04x:1A01H\n", z80_get_reg(Z80_REG_PC));
		}
		return 0x00;
		break;
	case 0x1c00:
	case 0x1b00:
		return m_AY8910->Read();
		break;
	}
	return ioport[_port];
}

void MucomX1Driver::WritePort(WORD _port, BYTE _data)
{
	switch (_port) {
	case 0x0000:
		if (bgm_flag[_data]) {
			memcpy(&ioport[0x5000], &bgm[BGM_SIZE * _data], BGM_SIZE);
			ram[LOAD_FLAG] = 0xff;
		}
		{
			TRACE("%04x, %04x: %02x %02x\n", z80_get_reg(Z80_REG_PC), _port, _data, ram[PLAY_FLAG]);
		}
		break;
	case 0x0700:
	case 0x0701:
		m_YM2151->Write(_port, _data);
		{
			TRACE("OPM:%04x: %02x\n", _port, _data);
		}
		break;
	case 0x1c00:
	case 0x1b00:
		m_AY8910->Write(_port >> 8, _data);
		break;
	}
	ioport[_port] = _data;
}

BYTE MucomX1Driver::sReadPort(WORD _port)
{
	return MucomX1Driver::m_Instance->ReadPort(_port);
}

void MucomX1Driver::sWritePort(WORD _port, BYTE _data)
{
	MucomX1Driver::m_Instance->WritePort(_port, _data);
}

void MucomX1Driver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	Unlock();
}

bool MucomX1Driver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	int opm_mix = 0x100;
	int psg_mix = 0xef;

	opm_mix = _config->GetOption("opm_mix", opm_mix);
	psg_mix = _config->GetOption("psg_mix", psg_mix);

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x2000);
			} else if (i->type == "data") {
				zip.Load(i->filename, ioport + i->offset, 0x800);
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
	z80_set_reg(Z80_REG_PC, 0xc000);

	//m_YM2151->Initialize(3579545);
	m_YM2151->Initialize(4000000);
	m_YM2151->SetVolume(0, 0, opm_mix);
	if (m_type == TYPE_GENERIC) {
		sdm->RegisterSoundChip(m_YM2151);
	}

	//m_AY8910->Initialize(3579545 / 2);
	m_AY8910->Initialize(2000000);
	m_AY8910->SetVolume(0, 0, psg_mix);
	sdm->RegisterSoundChip(m_AY8910);

	z80_emulate(500);

	return true;
}

void MucomX1Driver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case TIMER_INT:
		z80_raise_IRQ(0);
		z80_lower_IRQ();
		break;
	case VSYNC_INT:
		z80_raise_IRQ(6);
		z80_lower_IRQ();
		break;
	default:
		break;
	}
	Unlock();
}

bool MucomX1Driver::Play(DWORD _code)
{
	Lock();
	ram[PLAY_FLAG] = 0x01;
	ram[PLAY_CODE] = _code;
	Unlock();
	return true;
}

bool MucomX1Driver::Stop()
{
	Lock();
	ram[PLAY_FLAG] = 0xff;
	Unlock();
	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"mucom88",
		"x1",
		{"MUCOM88 (X1(OPM))", "SORCERIAN"},
		{
			// Files
			{"code",	{"main memory"}},
			{"data",	{"I/O port memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
		{
			// Options
			{"opm_mix",		0x100,		{"YM2151 mixing level"}},
			{"psg_mix",		0xef,		{"AY-3-8910 mixing level"}},
		},
	},
	{
		"mucom88",
		"x1psg",
		{"MUCOM88 (X1(PSG))", "SORCERIAN"},
		{
			// Files
			{"code",	{"main memory"}},
			{"data",	{"I/O port memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
		{
			// Options
			{"opm_mix",		0x100,		{"YM2151 mixing level"}},
			{"psg_mix",		0xef,		{"AY-3-8910 mixing level"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "mucom88") {
		if (_config->driver_subtype == "x1") {
			return MucomX1Driver::Instance(MucomX1Driver::TYPE_GENERIC);
		} else if (_config->driver_subtype == "x1psg") {
			return MucomX1Driver::Instance(MucomX1Driver::TYPE_PSGONLY);
		}
	}
	return NULL;
}

static ssDriverRegister mucomx1(string("mucom88"), CreateDriver, Description);
