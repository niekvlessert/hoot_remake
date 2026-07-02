#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2608.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class Ed2Driver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_ED2,
		TYPE_SORCVA,
	};
	static Ed2Driver *Instance(int _type = TYPE_GENERIC);
private:
	static Ed2Driver *m_Instance;
protected:
	Ed2Driver(int _type = TYPE_GENERIC);
public:
	virtual ~Ed2Driver();

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
	};
	enum {
		BGM_SIZE = 16 * 1024,
		PLAY_FLAG = 0x4010,
		PLAY_CODE = 0x4011,
	};

	int m_type;
	
	ssYM2608 *m_YM2608;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	bool bgm_flag[128];
	BYTE bgm[BGM_SIZE * 128];
};

Ed2Driver *Ed2Driver::m_Instance = NULL;

Ed2Driver *Ed2Driver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new Ed2Driver(_type);
	}
	return m_Instance;
}

Ed2Driver::Ed2Driver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}

	memset(bgm, 0, sizeof(bgm));
	for (int b = 0; b < 32; b++) {
		bgm_flag[b] = false;
	}
}

Ed2Driver::~Ed2Driver()
{
	if (m_YM2608) {
		delete m_YM2608;
	}
	m_Instance = NULL;
}

void Ed2Driver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE Ed2Driver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void Ed2Driver::sWriteCtrl(WORD _adr, BYTE _data)
{
	Ed2Driver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE Ed2Driver::sReadCtrl(WORD _adr)
{
	return Ed2Driver::m_Instance->ReadCtrl(_adr);
}

BYTE Ed2Driver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
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

void Ed2Driver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		if (bgm_flag[_data]) {
			memcpy(&ram[0xc000], &bgm[BGM_SIZE * _data], BGM_SIZE);
			if (m_type == TYPE_SORCVA) {
				BYTE tmp[18];
				memcpy(tmp, &ram[0xc000], 18);
				memcpy(&ram[0xc000], &tmp[12], 6);
				memcpy(&ram[0xc006], &tmp[0], 12);
				ram[0xc012] = 0xfd;
				ram[0xc013] = 0xff;
			}
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

BYTE Ed2Driver::sReadPort(WORD _port)
{
	return Ed2Driver::m_Instance->ReadPort(_port);
}

void Ed2Driver::sWritePort(WORD _port, BYTE _data)
{
	Ed2Driver::m_Instance->WritePort(_port, _data);
}

void Ed2Driver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	//z80_emulate((int)(8000000.0 * _second));
	Unlock();
}

bool Ed2Driver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	int fm_mix = _config->GetOption("fm_mix", 0);
	int ssg_mix = _config->GetOption("ssg_mix", -10);
	int rhythm_mix = _config->GetOption("rhythm_mix", 0);

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x4000);
			} else if (i->type == "bgm") {
				bgm_flag[i->offset] = true;
				zip.Load(i->filename, bgm + i->offset * BGM_SIZE, BGM_SIZE);
			}
		}

		zip.Close();
	}

	if (m_type == TYPE_SORCVA) {
		ram[0xbffd] = 0xff;
		ram[0xbffe] = 0x00;
		ram[0xbfff] = 0x00;
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);

	z80_add_read(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_add_write(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_reset();

	z80_set_reg(Z80_REG_PC, 0x4000);

	m_YM2608->Initialize(7987200, 0, 0);
	if (m_type == TYPE_SORCVA) {
		m_YM2608->SetVolumeDev(0, fm_mix);
		m_YM2608->SetVolumeDev(1, ssg_mix);
		m_YM2608->SetVolumeDev(2, rhythm_mix);
	}
	m_YM2608->SetVolume(0, 0, 0x100);
	m_YM2608->SetVolume(2, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2608);

	z80_emulate(5000);
	//m_YM2608->Write(0, 0x27);
	//m_YM2608->Write(1, 0x00);
	//m_YM2608->Write(1, 0x3f);

	return true;
}

void Ed2Driver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2608->TimerAOver();
#if 0
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
	default:
		break;
	}
	Unlock();
}

bool Ed2Driver::Play(DWORD _code)
{
	Lock();

	ram[PLAY_FLAG] = 0x01;
	ram[PLAY_CODE] = _code;

	Unlock();
	return true;
}

bool Ed2Driver::Stop()
{
	Lock();

	ram[PLAY_FLAG] = 0xff;
	ram[PLAY_CODE] = 0xff;

	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"mucom88",
		"ed2",
		{"MUCOM88 OPNA version (PC-88/PC-98)", "EIYU DENSETSU 2", "etc..."},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	{
		"mucom88",
		"sorcva",
		{"SOURCERIAN (PC-88VA)", "use driver for EIYU DENSETSU 2"},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
		{
			// Options
			{"fm_mix",		0,		{"FM volume (1/2dB)"}},
			{"ssg_mix",		-10,	{"SSG volume (1/2dB)"}},
			{"rhythm_mix",	0,		{"RHYTHM volume (1/2dB)"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "mucom88") {
		if (_config->driver_subtype == "ed2") {
			return Ed2Driver::Instance(Ed2Driver::TYPE_ED2);
		} else if (_config->driver_subtype == "sorcva") {
			return Ed2Driver::Instance(Ed2Driver::TYPE_SORCVA);
		}
	}
	return NULL;
}

static ssDriverRegister mucom88(string("mucom88"), CreateDriver, Description);
