#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2608.h"

#include "ssUnZip.h"

#include "mame/cpu/cpuintrf.h"
#include "mame/cpu/m68000.h"


class Mucom2608Driver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static Mucom2608Driver *Instance(int _type = TYPE_GENERIC);
private:
	static Mucom2608Driver *m_Instance;
protected:
	Mucom2608Driver(int _type = TYPE_GENERIC);
public:
	virtual ~Mucom2608Driver();

	bool Initialize(ssDriverConfig *_config);
	void Execute(double _second);
	void Interrupt(int _id);
	bool Play(DWORD _code);
	bool Stop();

private:
	static DWORD sReadDev(DWORD _adr);
	static void sWriteDev(DWORD _adr, DWORD _data);
private:
	DWORD ReadDev(DWORD _adr);
	void WriteDev(DWORD _adr, DWORD _data);
private:
	enum {
		TIMER_IDLE = 0,
		FM_TIMER_A,
		FM_TIMER_B,
	};
	enum {
		BGM_MAX = 128,
		BGM_SIZE = 24 * 1024,
	};

	int m_type;

	ssYM2608 *m_YM2608;

	BYTE rom[0x80000];
	BYTE ram[0x10000];

	bool bgm_flag[BGM_MAX];
	BYTE bgm[BGM_SIZE * BGM_MAX];

	int m_Code;
};

Mucom2608Driver *Mucom2608Driver::m_Instance = NULL;

Mucom2608Driver *Mucom2608Driver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new Mucom2608Driver(_type);
	}
	return m_Instance;
}

Mucom2608Driver::Mucom2608Driver(int _type)
{
	m_type = _type;

	m_YM2608 = new ssYM2608(FM_TIMER_A, FM_TIMER_B);

	memset(rom, 0, sizeof(rom));
	memset(ram, 0, sizeof(ram));
	memset(bgm, 0, sizeof(bgm));

	for (int b = 0; b < BGM_MAX; b++) {
		bgm_flag[b] = false;
	}
}

Mucom2608Driver::~Mucom2608Driver()
{
	if (m_YM2608) {
		delete m_YM2608;
	}
	m_Instance = NULL;
}


DWORD Mucom2608Driver::ReadDev(DWORD _adr)
{
	switch (_adr) {
	case 0xE00000:
		return m_Code;
		break;

	// YMF288
	case 0xecc0c1:
	case 0xecc0c3:
	case 0xecc0c5:
	case 0xecc0c7:
		return m_YM2608->Read((_adr>>1) & 3);
		break;
	default:
		TRACE("R %06x    %06x\n", _adr, m68000_get_pc());
		break;
	}

	return 0;
}

void Mucom2608Driver::WriteDev(DWORD _adr, DWORD _data)
{
	switch (_adr) {
		break;
	// YMF288
	case 0xecc0c1:
	case 0xecc0c3:
	case 0xecc0c5:
	case 0xecc0c7:
		m_YM2608->Write((_adr>>1) & 3, _data);
		break;
	default:
		TRACE("W %06x %06x %06x\n", _adr, _data, m68000_get_pc());
		break;
	}
}

DWORD Mucom2608Driver::sReadDev(DWORD _adr)
{
	return m_Instance->ReadDev(_adr);
}

void Mucom2608Driver::sWriteDev(DWORD _adr, DWORD _data)
{
	m_Instance->WriteDev(_adr, _data);
}

void Mucom2608Driver::Execute(double _second)
{
	Lock();
	m68000_emulate((int)(10000000 * _second));
	Unlock();
}

bool Mucom2608Driver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();
	int fm_mix = _config->GetOption("fm_mix", 19);
	int ssg_mix = _config->GetOption("ssg_mix", 0);
	int rhythm_mix = _config->GetOption("rhythm_mix", 19);

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, rom + i->offset, 0x10000);
			} if (i->type == "bgm") {
				bgm_flag[i->offset] = true;
				zip.Load(i->filename, bgm + i->offset * BGM_SIZE, BGM_SIZE);
			}
		}
		zip.Close();
	}

	m_Code = 0;

	m68000_init();
	cpu_init_memmap24();

	cpu_map_fetch(0x000000, 0x07ffff, &rom[0x000000]);
	cpu_map_read(0x000000, 0x07ffff, &rom[0x000000]);
	cpu_map_write(0x000000, 0x07ffff, &rom[0x000000]);

	cpu_map_fetch(0xf00000, 0xf0ffff, &ram[0x000000]);
	cpu_map_read(0xf00000, 0xf0ffff, &ram[0x000000]);
	cpu_map_write(0xf00000, 0xf0ffff, &ram[0x000000]);

	cpu_add_read(0xe00000, 0xefffff, CPU_MAP_HANDLED, sReadDev);
	cpu_add_write(0xe00000, 0xefffff, CPU_MAP_HANDLED, sWriteDev);

	cpu_end_memmap();

	m68000_reset(0);

	m_YM2608->Initialize(7987200, NULL, 0);
	m_YM2608->SetVolumeDev(0, fm_mix);
	m_YM2608->SetVolumeDev(1, ssg_mix);
	m_YM2608->SetVolumeDev(2, rhythm_mix);
	m_YM2608->SetVolume(0, 0, 0x100);
	m_YM2608->SetVolume(2, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2608);

	return true;
}

void Mucom2608Driver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2608->TimerAOver();
		m68000_raise_IRQ(MC68000_IRQ_6);
		m68000_lower_IRQ(MC68000_IRQ_6);
		break;
	case FM_TIMER_B:
		m_YM2608->TimerBOver();
		m68000_raise_IRQ(MC68000_IRQ_6);
		m68000_lower_IRQ(MC68000_IRQ_6);
		break;
	default:
		break;
	}
	Unlock();
}

bool Mucom2608Driver::Play(DWORD _code)
{
	Lock();

	if (bgm_flag[_code]) {
		memcpy(&rom[0x20000], &bgm[BGM_SIZE * _code], BGM_SIZE);
	}
	m_Code = _code;
	m68000_cause_NMI();

	Unlock();
	return true;
}

bool Mucom2608Driver::Stop()
{
	Lock();

	m_Code = 0x80;
	m68000_cause_NMI();

	Unlock();
	return true;
}

// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"mucom88",
		"pc98",
		{"MUCOM88 (PC-98)", "Revival Xanadu", "etc..."},
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
	if (_config->driver_majortype == "mucom88") {
		if (_config->driver_subtype == "pc98") {
			return Mucom2608Driver::Instance(Mucom2608Driver::TYPE_GENERIC);
		}
	}
	return NULL;
}

//static ssDriverRegister mucom2608(string("mucom88"), CreateDriver, Description);
static ssDriverRegister mucom2608(string("mucom88"), CreateDriver);
