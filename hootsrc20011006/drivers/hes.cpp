#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssMemoryDump.h"
#include "sound/ssPCEPSG.h"

#include "ssFile.h"
#include "ssUnZip.h"

#include "mame/cpu/cpuintrf.h"
#include "mame/cpu/h6280.h"

class HesDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static HesDriver *Instance(int _type = TYPE_GENERIC);
private:
	static HesDriver *m_Instance;
protected:
	HesDriver(int _type = TYPE_GENERIC);
public:
	virtual ~HesDriver();

	bool Initialize(ssDriverConfig *_config);
	void Execute(double _second);
	void Interrupt(int _id);
	bool Play(DWORD _code);
	bool Stop();

private:
	static void sWriteIO(DWORD _adr, BYTE _data);
	static DWORD sReadIO(DWORD _adr);
private:
	void WriteIO(DWORD _adr, BYTE _data);
	DWORD ReadIO(DWORD _adr);
private:
	enum {
		TIMER_IDLE = 0,
		TIMER_INT,
		VSYNC_INT,
	};
	enum {
		CPU_CLOCK = 21477270/3,
		//CPU_CLOCK = 3579545,
	};

	int m_type;
	int m_cpuclock;

	bool m_playing;

	ssPCEPSG *m_PSG;
	ssMemoryDump *m_MemDump;

	BYTE rom[0x100000];
	BYTE ram[0x8000];
	BYTE bios[0x2000];

	BYTE m_start_song;
	WORD m_req_adr;
	BYTE m_mpr[8];

	ssTimer *m_Timer;
	ssTimer *m_VSync;

	int m_timer_value;
	int m_timer_status;
	int m_timer_ack;
};

HesDriver *HesDriver::m_Instance = NULL;

HesDriver *HesDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new HesDriver(_type);
	}
	return m_Instance;
}

HesDriver::HesDriver(int _type)
{
	m_type = _type;

	m_playing = false;

	m_PSG = new ssPCEPSG;
	m_MemDump = new ssMemoryDump;

	m_Timer = NULL;
	m_VSync = NULL;

	m_timer_value = 0;
	m_timer_status = 0;
	m_timer_ack = 1;

	memset(rom, 0, sizeof(rom));
	memset(ram, 0, sizeof(ram));
	memset(bios, 0, sizeof(bios));
}

HesDriver::~HesDriver()
{
	if (m_Timer) {
		delete m_Timer;
	}
	if (m_VSync) {
		delete m_VSync;
	}
	if (m_PSG) {
		delete m_PSG;
	}
	if (m_MemDump) {
		delete m_MemDump;
	}
	m_Instance = NULL;
}

void HesDriver::WriteIO(DWORD _adr, BYTE _data)
{
	//ram[_adr] = _data;
	const DWORD adr = _adr;
	const BYTE data = _data;

	switch (adr) {
	case 0x1fe800:
	case 0x1fe801:
	case 0x1fe802:
	case 0x1fe803:
	case 0x1fe804:
	case 0x1fe805:
	case 0x1fe806:
	case 0x1fe807:
	case 0x1fe808:
	case 0x1fe809:
		m_PSG->Write(adr & 0x0f, data);
		break;
	case 0x1fec00:
		{
			//H6280_timer_w(0, data);
			m_timer_value = data;
			m_Timer->SetInterval((double)((m_timer_value&127)+1) * 1024./ (double)m_cpuclock);
		}
		break;
	case 0x1fec01:
		{
			//H6280_timer_w(1, data);
			m_timer_status = data & 1;
		}
		break;
	case 0x1ff402:
		H6280_irq_status_w(0, data);
		break;
	case 0x1ff403:
		H6280_irq_status_w(1, data);
		{
			m_timer_ack = 1;
			h6280_lower_TIMER();
		}
		break;
	case 0x1ffffe:
		cpu_map_fetch(0x000000, 0x001fff, &rom[0x0000]);
		cpu_map_read(0x000000, 0x001fff, &rom[0x0000]);
		h6280_skip_idle();
		break;
	case 0x1fffff:
		h6280_skip_idle();
		break;
	}
}

DWORD HesDriver::ReadIO(DWORD _adr)
{
	static int x = 0;
	const DWORD adr = _adr;

	switch (adr) {
	case 0x1fe000:
		return 0x20;
	case 0x1fe001:
		return 0;
	case 0x1fe002:
		return 0;
	case 0x1fe800:
	case 0x1fe801:
	case 0x1fe802:
	case 0x1fe803:
	case 0x1fe804:
	case 0x1fe805:
	case 0x1fe806:
	case 0x1fe807:
	case 0x1fe808:
	case 0x1fe809:
		return m_PSG->Read(adr & 0x0f);
	case 0x1fec00:
		//return H6280_timer_r(0);
		return m_timer_value;
	case 0x1fec01:
		//return H6280_timer_r(1);
		return m_timer_status;
	case 0x1ff402:
		return H6280_irq_status_r(0);
	case 0x1ff403:
		return H6280_irq_status_r(1);
	}
	return 0xff;
}

void HesDriver::sWriteIO(DWORD _adr, BYTE _data)
{
	//TRACE("%04x:%02x\n", _adr, _data);
	m_Instance->WriteIO(_adr, _data);
}

DWORD HesDriver::sReadIO(DWORD _adr)
{
	//TRACE("%04x:%02x\n", _adr, _data);
	return m_Instance->ReadIO(_adr);
}

void HesDriver::Execute(double _second)
{
	Lock();

	if (m_playing) {
		//h6280_emulate((int)(m_cpuclock * _second));
		h6280_emulate(m_cpuclock); // Ä“ü‚µ‚È‚¢‚æ‚¤‚É[•ª‚ÈŽžŠÔ
	}
	m_MemDump->SetWORD(0, h6280_get_pc());

	Unlock();
}

static DWORD GetDWORD(BYTE *p)
{
	DWORD ret = (p[3]<<24) + (p[2]<<16) + (p[1]<<8) + p[0];
	return ret;
}

static WORD GetWORD(BYTE *p)
{
	WORD ret = (p[1]<<8) + p[0];
	return ret;
}

bool HesDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	m_cpuclock = _config->GetOption("cpuclock", CPU_CLOCK);

	if (_config->IsSingleFile()) {
		ssFile file;
		if (file.Open(_config->archive)) {
			file.Read(rom, sizeof(rom));
		}
		for (DWORD i = 0; i < 0x100; i++) {
			char title[32];
			sprintf(title, "SOUND CODE %02x", i);
			_config->AddTitle(i, string(title));
		}
	} else {
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, rom, sizeof(rom));
			}
		}

		zip.Close();
	}

	{
		int i;
		DWORD data_size;
		DWORD load_adr;

		m_start_song = rom[0x05];
		m_req_adr = GetWORD(&rom[0x06]);
		for (i = 0; i < 8; i++) {
			m_mpr[i] = rom[0x08 + i];
		}

		data_size = GetDWORD(&rom[0x14]);
		load_adr = GetDWORD(&rom[0x18]);

		memmove(&rom[load_adr], &rom[0x20], data_size);

		//for (i = 0; i < 8; i++) {
		//	h6280_set_reg(H6280_M1 + i, m_mpr[i]);
		//}
	}

	h6280_init();
	cpu_init_memmap21();

	cpu_map_fetch(0x000000, 0x0fffff, &rom[0x0000]);
	cpu_map_read(0x000000, 0x0fffff, &rom[0x0000]);

	cpu_map_fetch(0x1f0000, 0x1f7fff, &ram[0x0000]);
	cpu_map_read(0x1f0000, 0x1f7fff, &ram[0x0000]);
	cpu_map_write(0x1f0000, 0x1f7fff, &ram[0x0000]);

	cpu_add_write(0x1fe000, 0x1fffff, CPU_MAP_HANDLED, sWriteIO);
	cpu_add_read(0x1fe000, 0x1fffff, CPU_MAP_HANDLED, sReadIO);

	memset(bios, 0xea, sizeof(bios));
	bios[0x1ffe] = 0x00; // Reset
	bios[0x1fff] = 0xf0;

	bios[0x1ff6] = 0xf5; // IRQ2
	bios[0x1ff7] = 0xff;
	bios[0x1ff8] = 0xf5; // IRQ1
	bios[0x1ff9] = 0xff;
	bios[0x1ffa] = 0xf5; // TIMER
	bios[0x1ffb] = 0xff;
	bios[0x1ffc] = 0xf5; // NMI
	bios[0x1ffd] = 0xff;

	bios[0x1ff5] = 0x40; // rti

	// load -- f000h
	bios[0x1000] = 0x78; // sei
	bios[0x1001] = 0xa9; // lda #$00
	bios[0x1002] = m_mpr[0];
	bios[0x1003] = 0x53; // tam #$01
	bios[0x1004] = 0x01;
	bios[0x1005] = 0xa9; // lda #$00
	bios[0x1006] = m_mpr[1];
	bios[0x1007] = 0x53; // tam #$02
	bios[0x1008] = 0x02;
	bios[0x1009] = 0xa9; // lda #$00
	bios[0x100a] = m_mpr[2];
	bios[0x100b] = 0x53; // tam #$04
	bios[0x100c] = 0x04;
	bios[0x100d] = 0xa9; // lda #$00
	bios[0x100e] = m_mpr[3];
	bios[0x100f] = 0x53; // tam #$08
	bios[0x1010] = 0x08;
	bios[0x1011] = 0xa9; // lda #$00
	bios[0x1012] = m_mpr[4];
	bios[0x1013] = 0x53; // tam #$10
	bios[0x1014] = 0x10;
	bios[0x1015] = 0xa9; // lda #$00
	bios[0x1016] = m_mpr[5];
	bios[0x1017] = 0x53; // tam #$20
	bios[0x1018] = 0x20;
	bios[0x1019] = 0xa9; // lda #$00
	bios[0x101a] = m_mpr[6];
	bios[0x101b] = 0x53; // tam #$40
	bios[0x101c] = 0x40;
	bios[0x101d] = 0xa9; // lda #$00
	bios[0x101e] = m_mpr[7];
	bios[0x101f] = 0x53; // tam #$80
	bios[0x1020] = 0x80;

	bios[0x1021] = 0x4c; // jmp 0000h
	bios[0x1022] = 0x00;
	bios[0x1023] = 0x00;

	// play
	bios[0x0000] = 0x8d; // sta $1ffe
	bios[0x0001] = 0xfe;
	bios[0x0002] = 0x1f;

	bios[0x0010] = 0xa9; // lda song_no
	bios[0x0011] = 0x00; // SONG_NO
	bios[0x0012] = 0x20; // jsr req_adr
	bios[0x0013] = m_req_adr;
	bios[0x0014] = m_req_adr >> 8;

	bios[0x0020] = 0x8d; // sta $1fff
	bios[0x0021] = 0xff;
	bios[0x0022] = 0x1f;

	bios[0x002d] = 0x4c; // jmp 0020h
	bios[0x002e] = 0x20;
	bios[0x002f] = 0x00;

	cpu_map_fetch(0x1fe000, 0x1fffff, &bios[0x0000]);

	cpu_end_memmap();

	h6280_reset(0);
	h6280_init();

	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(0.1);
	m_VSync = new ssTimer(VSYNC_INT);
	m_VSync->SetInterval(0.01640);

	m_PSG->Initialize(3579545);
	m_PSG->SetVolume(0, 0, 0x100);
	sdm->RegisterSoundChip(m_PSG);

	//sdm->RegisterSoundChip(m_MemDump);

	//h6280_emulate(5000);
	//m_playing = true;

	return true;
}

void HesDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case TIMER_INT:
		if (m_playing && m_timer_status && m_timer_ack) {
			h6280_raise_TIMER();
		}
		break;
	case VSYNC_INT:
		if (m_playing) {
			h6280_raise_IRQ1();
			h6280_lower_IRQ1();
		}
		break;
	default:
		break;
	}
	Unlock();
}

bool HesDriver::Play(DWORD _code)
{
	Lock();

	memset(ram, 0, sizeof(ram));

	cpu_map_fetch(0x000000, 0x001fff, &bios[0x0000]);
	cpu_map_read(0x000000, 0x001fff, &bios[0x0000]);

	bios[0x0011] = _code; // SONG_NO

	h6280_reset(0);
	h6280_init();
	h6280_emulate(500);

	m_playing = true;

	Unlock();

	return true;
}

bool HesDriver::Stop()
{
	int ch;

	for (ch = 0; ch < 6; ch++) {
		m_PSG->Write(0, ch);
		m_PSG->Write(4, 0);
	}

	m_playing = false;

	return true;
}

// ƒhƒ‰ƒCƒoî•ñ
static ssDriverDescription Description[] =
{
	{
		"pcengine",
		"hes",
		{"PC-ENGINE (HES format)"},
		{
			// Files
			{"code",	{"HES data"}},
		},
		{
			// Options
			{"cpuclock",	21477270/3,		{"HuC6280 clock"}},
		},
	},
	0
};

// ƒhƒ‰ƒCƒo‚Ì“o˜^
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "pcengine") {
		if (_config->driver_subtype == "hes") {
			return HesDriver::Instance(HesDriver::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister hes(string("pcengine"), CreateDriver, Description);
