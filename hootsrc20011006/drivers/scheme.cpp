#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2608.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class SchemeDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_SCHEME,
		TYPE_YK2,
	};

	enum {
		SUBTYPE_GODHAND = 0,
		SUBTYPE_AKDN,
	};
	static SchemeDriver *Instance(int _type = TYPE_GENERIC);
private:
	static SchemeDriver *m_Instance;
protected:
	SchemeDriver(int _type = TYPE_GENERIC);
public:
	virtual ~SchemeDriver();

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
		// SCHEME (OPNA)
		BGM_SIZE = 8 * 1024,
		PLAY_FLAG = 0x9010,
		PLAY_CODE = 0x9011,
		LOAD_FLAG = 0x9012,
		BGM_BANK = 0x9013,

		// YK-2 DISK
		YK2_PLAY_FLAG = 0x0100,
		YK2_PLAY_CODE = 0x0101,
		YK2_LOAD_FLAG = 0x0102,
	};

	int m_type;
	int m_subtype;

	ssYM2608 *m_YM2608;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	BYTE pcmdata[0x40000];

	bool bgm_flag[128];
	BYTE bgm[BGM_SIZE * 128];
};

SchemeDriver *SchemeDriver::m_Instance = NULL;

SchemeDriver *SchemeDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new SchemeDriver(_type);
	}
	return m_Instance;
}

SchemeDriver::SchemeDriver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);
	//m_Timer = new ssTimer(TIMER_INT);
	//m_Timer->SetInterval(1.0/600.0);
	m_Timer = 0;
	//m_Timer->SetInterval(1.0/60.0);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	memset(pcmdata, 0x80, sizeof(pcmdata));

	m_subtype = 0;

	for (int b = 0; b < 128; b++) {
		bgm_flag[b] = false;
	}

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}
}

SchemeDriver::~SchemeDriver()
{
	if (m_YM2608) {
		delete m_YM2608;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	m_Instance = NULL;
}

void SchemeDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE SchemeDriver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void SchemeDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	SchemeDriver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE SchemeDriver::sReadCtrl(WORD _adr)
{
	return SchemeDriver::m_Instance->ReadCtrl(_adr);
}

BYTE SchemeDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case 0x01:
		if (m_type == TYPE_YK2) {
			z80_skip_idle();
		}
		return 0;
	case 0x02:
		if (m_type == TYPE_YK2) {
			ram[YK2_PLAY_FLAG] = 0;
		}
		return 0;
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
	return ioport[port & 0xff];
}

void SchemeDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		if (bgm_flag[_data]) {
			if (m_type == TYPE_SCHEME) {
				memcpy(&ram[0xc000], &bgm[BGM_SIZE * _data], BGM_SIZE);
				ram[LOAD_FLAG] = 0xff;
			} else if (m_type == TYPE_YK2) {
				ram[YK2_LOAD_FLAG] = 0xff;
				memcpy(&ram[0xc200], &bgm[BGM_SIZE * _data], BGM_SIZE);
				ram[YK2_LOAD_FLAG] = 0x00;
				ram[YK2_PLAY_FLAG] = 0x00;
			}
			TRACE("BGM #%02x Loaded\n", _data);
		}
		break;
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
		m_YM2608->Write(_port, _data);
//		TRACE("%04x OPNA(%02x) : %02x\n",
//			z80_get_reg(Z80_REG_PC), port, _data);
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
	ioport[port & 0xff] = _data;
}

BYTE SchemeDriver::sReadPort(WORD _port)
{
	return SchemeDriver::m_Instance->ReadPort(_port);
}

void SchemeDriver::sWritePort(WORD _port, BYTE _data)
{
	SchemeDriver::m_Instance->WritePort(_port, _data);
}

void SchemeDriver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	//z80_emulate((int)(8000000.0 * _second));
	Unlock();
}

bool SchemeDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	m_subtype = _config->GetOption("subtype", m_subtype);

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x4000);
			} else if (i->type == "adpcm") {
				int p = i->offset;
				WORD *pcmadr;

				if (m_type == TYPE_SCHEME) {
					pcmadr = (WORD *)&ram[0xef00];
					zip.Load(i->filename, &pcmdata[pcmadr[p*2] * 4], (pcmadr[p*2+1] - pcmadr[p*2]) * 4 + 3);
				} else if (m_type == TYPE_YK2) {
					if (m_subtype == SUBTYPE_GODHAND) {
						pcmadr = (WORD *)&ram[0xe500];
					} else if (m_subtype == SUBTYPE_AKDN) {
						pcmadr = (WORD *)&ram[0xaf00];
					}
					zip.Load(i->filename, &pcmdata[pcmadr[p*4] * 4], (pcmadr[p*4+1] - pcmadr[p*4]) * 4 + 3);
				}
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

	if (m_type == TYPE_SCHEME) {
		z80_set_reg(Z80_REG_PC, 0x9000);
	} else if (m_type == TYPE_YK2) {
		z80_set_reg(Z80_REG_PC, 0x0000);
		// ADPCM 使用、ポートの設定
		switch (m_subtype) {
			case SUBTYPE_GODHAND:
				ram[0xfffa] = 0x01;
				ram[0xfffc] = 0x32;
				ram[0xfffd] = 0x44;
				ram[0xfffe] = 0x46;
				break;
			case SUBTYPE_AKDN:
				ram[0x79d7] = 0x39;
				ram[0x79d8] = 0x33;
				ram[0xfffa] = 0x01;
				ram[0xfffc] = 0x32;
				ram[0xfffd] = 0x44;
				ram[0xfffe] = 0x46;
				ram[0xffff] = 0x0a;
				break;
			default:
				break;
		}
	}

	m_YM2608->Initialize(7987200,
		pcmdata, sizeof(pcmdata));
	m_YM2608->SetVolume(0, 0, 0x100);
	m_YM2608->SetVolume(2, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2608);

	z80_emulate(500);

	return true;
}

void SchemeDriver::Interrupt(int _id)
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
			if (m_type == TYPE_SCHEME) {
				z80_raise_IRQ(8);
			} else if (m_type == TYPE_YK2) {
				z80_raise_IRQ(0xff);
			}
			z80_lower_IRQ();
		}
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	case TIMER_INT:
		//z80_raise_IRQ(4);
		//z80_lower_IRQ();
		//TRACE("B Tick:%d\n", GetTickCount());
		break;
	default:
		break;
	}
	Unlock();
}

bool SchemeDriver::Play(DWORD _code)
{
	Lock();

	if (m_type == TYPE_SCHEME) {
		ram[PLAY_FLAG] = 0x01;
		ram[PLAY_CODE] = _code;
		ram[BGM_BANK] = _code>>8;
	} else if (m_type == TYPE_YK2) {
		ram[YK2_PLAY_FLAG] = 0x01;
		ram[YK2_PLAY_CODE] = _code;
	}

	Unlock();
	return true;
}

bool SchemeDriver::Stop()
{
	Lock();

	if (m_type ==  TYPE_SCHEME) {
		ram[PLAY_FLAG] = 0xff;
	} else if (m_type == TYPE_YK2) {
		ram[YK2_PLAY_FLAG] = 0xff;
	}
	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"mucom88",
		"scheme",
		{"THE SCHEME"},
		{
			// Files
			{"code",	{"main memory"}},
			{"adpcm",	{"ADPCM data"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "mucom88") {
		if (_config->driver_subtype == "scheme") {
			return SchemeDriver::Instance(SchemeDriver::TYPE_SCHEME);
		}
		else if (_config->driver_subtype == "yk-2disk") {
			return SchemeDriver::Instance(SchemeDriver::TYPE_YK2);
		}
	}
	return NULL;
}

static ssDriverRegister mucom88(string("mucom88"), CreateDriver, Description);
