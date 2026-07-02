//
// X680x0 いんちきエンジン
//
// Sat Nov 25 21:00 JST 2000 (Fu-.)
#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2151.h"
#include "sound/ssMSM6258V.h"
#include "sound/ssMemoryDump.h"

#include "ssUnZip.h"

#include "mame/cpu/cpuintrf.h"
#include "mame/cpu/m68000.h"

class X68kDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static X68kDriver *Instance(int _type = TYPE_GENERIC);
private:
	static X68kDriver *m_Instance;
protected:
	X68kDriver(int _type = TYPE_GENERIC);
public:
	virtual ~X68kDriver();

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

	int m_type;

	ssMSM6258V *m_ADPCM;
	ssYM2151 *m_YM2151;
	ssMemoryDump *m_MemDump;

	BYTE rom[0x80000];
	BYTE ram[0x10000];

	int m_Flag;
	int m_Code;
};

X68kDriver *X68kDriver::m_Instance = NULL;

X68kDriver *X68kDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new X68kDriver(_type);
	}
	return m_Instance;
}

X68kDriver::X68kDriver(int _type)
{
	m_type = _type;

	m_ADPCM = new ssMSM6258V;
	m_YM2151 = new ssYM2151(FM_TIMER_A, FM_TIMER_B);
	m_MemDump = new ssMemoryDump;

	memset(rom, 0, sizeof(rom));
	memset(ram, 0, sizeof(ram));
}

X68kDriver::~X68kDriver()
{
	if (m_ADPCM) {
		delete m_ADPCM;
	}
	if (m_YM2151) {
		delete m_YM2151;
	}
	if (m_MemDump) {
		delete m_MemDump;
	}
	m_Instance = NULL;
}

DWORD X68kDriver::ReadDev(DWORD _adr)
{
	switch (_adr) {

	// SOUND CODE
	case 0xe00000:
		return m_Flag;
		break;

	case 0xe00001:
//		TRACE("Read\n");
		return m_Code;
		break;

	case 0xe00002:
		return m_Code >> 8;
		break;

	// EMULATOR IDLE
	case 0xe00800:
		m68000_ICount = 0;
		break;

	// YM-2151
	case 0xe90003:
		return m_YM2151->Read((_adr>>1) & 3);
		break;

	// MSM6258V
	case 0xe9a005:
		return m_ADPCM->GetPanAndRate();
		break;

	default:
//		TRACE("R %06x    %06x\n", _adr, m68000_get_pc());
		break;
	}

	return 0;
}

void X68kDriver::WriteDev(DWORD _adr, DWORD _data)
{
	switch (_adr) {

	// SOUND FLAG
	case 0xe00000:
		m_Flag = (BYTE)_data;
		return;
		break;

	// YM-2151
	case 0xe90001:
	case 0xe90003:
		m_YM2151->Write((_adr>>1) & 1, _data);
		break;

	// MSM6258V
	case 0xe840c0:						// DMA 動作モード設定
		if (_data == 0xff) {
			// 再生停止
			m_ADPCM->SetStat(0);		// 0x88 でなければなんでもいい
		}
		break;

	case 0xe840ca:		// サイズの設定
	{
		WORD _size = m68000_get_reg(M68K_D2);

		m_ADPCM->SetAdpcmSize(_size);
		break;
	}

	case 0xe840cc:		// アドレスの設定
	{
		BYTE *pcm = rom + m68000_get_reg(M68K_A1);

		m_ADPCM->SetAdpcmAddr(pcm);
		break;
	}

	// 再生開始・停止
	case 0xe840c7:
		m_ADPCM->SetStat(_data & 0xff);
		break;

	// 再生レート・パン変更
	case 0xe9a005:
		m_ADPCM->SetPanAndRate(_data);
		break;

	default:
//		TRACE("W %06x %06x %06x\n", _adr, _data, m68000_get_pc());
		break;
	}
}

DWORD X68kDriver::sReadDev(DWORD _adr)
{
	return m_Instance->ReadDev(_adr);
}

void X68kDriver::sWriteDev(DWORD _adr, DWORD _data)
{
	m_Instance->WriteDev(_adr, _data);
}

void X68kDriver::Execute(double _second)
{
	Lock();
	m68000_emulate((int)(10000000 * _second));
	Unlock();
}

bool X68kDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	int opm_mix = 0xc0;
	int pcm_mix = 0xf0;
	int viewadr = 0;

	opm_mix = _config->GetOption("opm_mix", opm_mix);
	pcm_mix = _config->GetOption("pcm_mix", pcm_mix);

	m_Flag = 0;
	m_Code = 0;
	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, rom + i->offset, 0x80000);
			}
		}

		zip.Close();
	}

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

	m_YM2151->Initialize(4000000);
	m_YM2151->SetVolume(0, 0, opm_mix);
	sdm->RegisterSoundChip(m_YM2151);

	m_ADPCM->Initialize(15600);
	m_ADPCM->SetVolume(0, 0, pcm_mix);
	sdm->RegisterSoundChip(m_ADPCM);

	// ダンプアドレスの設定
	viewadr = (rom[0x800] << 24) + (rom[0x801] << 16) + (rom[0x802] << 8) + rom[0x803];
	m_MemDump->SetAddress(&rom[viewadr]);
	sdm->RegisterSoundChip(m_MemDump);

//	m68000_emulate(5000);

	return true;
}

void X68kDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2151->TimerAOver();
		m68000_raise_IRQ(MC68000_IRQ_6);
		m68000_lower_IRQ(MC68000_IRQ_6);
		break;

	case FM_TIMER_B:
		m_YM2151->TimerBOver();
		m68000_raise_IRQ(MC68000_IRQ_6);
		m68000_lower_IRQ(MC68000_IRQ_6);
		break;
	default:
		break;
	}
	Unlock();
}

bool X68kDriver::Play(DWORD _code)
{
	Lock();

//	TRACE("Write\n");
	m_Code = _code;
	m_Flag = 0x01;

	Unlock();

	return true;
}

bool X68kDriver::Stop()
{
	m_Code = 0x5f;
	m_Flag = 0x01;

	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"x68k",
		"generic",
		{"X68000"},
		{
			// Files
			{"code",	{"main memory"}},
		},
		{
			// Options
			{"opm_mix",		0xc0,		{"YM2151 mixing level"}},
			{"pcm_mix",		0xf0,		{"MSM6258V mixing level"}},
		},
	},
	0
};

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "x68k") {
		if (_config->driver_subtype == "generic") {
			return X68kDriver::Instance(X68kDriver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister x68k(string("x68k"), CreateDriver, Description);
