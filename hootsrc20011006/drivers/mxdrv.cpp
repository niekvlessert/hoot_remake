#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2151.h"
#include "sound/ssPCM8.h"
#include "sound/ssMemoryDump.h"

#include "ssFile.h"
#include "ssUnZip.h"

#include "mame/cpu/cpuintrf.h"
#include "mame/cpu/m68000.h"

class Mxdrv : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
	};
	static Mxdrv *Instance(int _type = TYPE_GENERIC);
private:
	static Mxdrv *m_Instance;
protected:
	Mxdrv(int _type = TYPE_GENERIC);
public:
	virtual ~Mxdrv();

	bool Initialize(ssDriverConfig *_config);
	void Execute(double _second);
	void Interrupt(int _id);
	bool Play(DWORD _code);
	bool Stop();

private:
	static DWORD sReadDev(DWORD _adr);
	static void sWriteDev(DWORD _adr, DWORD _data);
private:
	bool LoadMDX(void);
	void Trap2(void);
	void Trap15(void);
	DWORD ReadDev(DWORD _adr);
	void WriteDev(DWORD _adr, DWORD _data);
private:
	enum {
		MDX_ADR    = 0x00c0,
		MDX_SIZE   = 0x00c4,
		PDX_ADR    = 0x00c8,
		PDX_SIZE   = 0x00cc,
		MDX_LOADED = 0x00d0,
		PDX_LOADED = 0x00d1,
		MDX_BANK_SIZE = 0x00f0,
		PDX_BANK_SIZE = 0x00f4,
	};
	enum {
		TIMER_IDLE = 0,
		FM_TIMER_A,
		FM_TIMER_B,
	};

	int m_type;

	ssPCM8 *m_ADPCM;
	ssYM2151 *m_YM2151;
	ssMemoryDump *m_MemDump;

	BYTE *mdx_file;
	BYTE *pdx_file;
	int mdx_file_size;
	int pdx_file_size;

	BYTE ram[0x10000];
	BYTE *mdx;
	BYTE *pdx;

	DWORD m_code;

	DWORD m_mdx_adr;
	DWORD m_mdx_bank_size;
	DWORD m_pdx_adr;
	DWORD m_pdx_bank_size;
};

Mxdrv *Mxdrv::m_Instance = NULL;

Mxdrv *Mxdrv::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new Mxdrv(_type);
	}
	return m_Instance;
}

Mxdrv::Mxdrv(int _type)
{
	m_type = _type;

	m_YM2151 = new ssYM2151(FM_TIMER_A, FM_TIMER_B);
	m_ADPCM = new ssPCM8;
	m_MemDump = new ssMemoryDump;

	memset(ram, 0, sizeof(ram));

	mdx_file = NULL;
	pdx_file = NULL;
	mdx = NULL;
	pdx = NULL;
}

Mxdrv::~Mxdrv()
{
	delete m_ADPCM;
	delete m_YM2151;
	delete m_MemDump;

	delete[] mdx_file;
	delete[] pdx_file;
	delete[] mdx;
	delete[] pdx;

	m_Instance = NULL;
}

DWORD Mxdrv::ReadDev(DWORD _adr)
{
	switch (_adr) {
	case 0xe00000:
		{
			m68000_ICount = 0;
			DWORD ret = m_code;
			m_code = 0;
			return ret;
		}
		break;
	case 0xe90003:
		return m_YM2151->Read((_adr>>1) & 3);
		break;
	}

	return 0;
}

void Mxdrv::WriteDev(DWORD _adr, DWORD _data)
{
	switch (_adr) {
	case 0xe00000:
		switch (_data) {
		case 2:
			Trap2();
			break;
		case 15:
			Trap15();
			break;
		}
		break;
	case 0xe90001:
	case 0xe90003:
		m_YM2151->Write((_adr>>1) & 1, _data);
		break;
	}
}

void Mxdrv::Trap2(void)
{
	const DWORD d0 = m68000_get_reg(M68K_D0);
	const DWORD d1 = m68000_get_reg(M68K_D1);
	const DWORD d2 = m68000_get_reg(M68K_D2);
	const DWORD a1 = m68000_get_reg(M68K_A1);
	const int code = (d0&0xfff0);
	const int ch = d0 & 0x000f;

	switch (code) {
	case 0x0000:
	case 0x0070:
		if (ch < 8) {
			const int vol = (d1 >> 16) & 0xff;
			const int freq = (d1 >> 8) & 0xff;
			const int pan = d1 & 0xff;
			const int size = d2;

			if (code == 0x0000 && size == 0) {
				// stop
				m_ADPCM->Stop(ch);
				break;
			}
			if (vol <= 15) {
				m_ADPCM->SetVol(ch, vol);
			}
			switch (freq) {
			case 0:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 3900);
				break;
			case 1:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 5200);
				break;
			case 2:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 7800);
				break;
			case 3:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 10400);
				break;
			case 4:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 15600);
				break;
			case 5:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_16BITPCM);
				m_ADPCM->SetFreq(ch, 15600);
				break;
			case 6:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_8BITPCM);
				m_ADPCM->SetFreq(ch, 15600);
				break;
			}
			if (pan <= 3) {
				m_ADPCM->SetPan(ch, pan);
			}
			if (code == 0x0000) {
				m_ADPCM->Stop(ch);
				int adr = a1 - m_pdx_adr;
				m_ADPCM->Play(ch, pdx, adr, size);
			}
		}
		break;
	}
}

void Mxdrv::Trap15(void)
{
	const DWORD d0 = m68000_get_reg(M68K_D0);
	const DWORD d1 = m68000_get_reg(M68K_D1);
	const DWORD d2 = m68000_get_reg(M68K_D2);
	const DWORD a1 = m68000_get_reg(M68K_A1);

	switch (d0 & 0xff) {
	case 0x60:
		{
			const int ch = 0;
			const int vol = 8;
			const int freq = (d1 >> 8) & 0xff;
			const int pan = d1 & 0xff;
			const int size = d2;

			m_ADPCM->SetVol(ch, vol);
			switch (freq) {
			case 0:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 3900);
				break;
			case 1:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 5200);
				break;
			case 2:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 7800);
				break;
			case 3:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 10400);
				break;
			case 4:
				m_ADPCM->SetMode(ch, ssADPCM::MODE_ADPCM);
				m_ADPCM->SetFreq(ch, 15600);
				break;
			}
			m_ADPCM->SetPan(ch, pan);

			int adr = a1 - m_pdx_adr;
			m_ADPCM->Play(ch, pdx, adr, size);
		}
		break;
	case 0x67:
		{
			switch (d1) {
			case 0:
				m_ADPCM->Stop(0);
				break;
			case 1:
				m_ADPCM->Stop(0);
				break;
			}
		}
		break;
	}
}

DWORD Mxdrv::sReadDev(DWORD _adr)
{
	return m_Instance->ReadDev(_adr);
}

void Mxdrv::sWriteDev(DWORD _adr, DWORD _data)
{
	m_Instance->WriteDev(_adr, _data);
}

void Mxdrv::Execute(double _second)
{
	Lock();
	m68000_emulate((int)(10000000 * _second));
	Unlock();
}

static void WriteWORD(BYTE *_p, WORD _d)
{
	_p[0] = _d >> 8;
	_p[1] = _d;
}

static void WriteDWORD(BYTE *_p, DWORD _d)
{
	_p[0] = _d >> 24;
	_p[1] = _d >> 16;
	_p[2] = _d >> 8;
	_p[3] = _d;
}

static DWORD ReadDWORD(BYTE *_p)
{
	DWORD ret = (_p[0]<<24) + (_p[1]<<16) + (_p[2]<<8) + _p[3];
	return ret;
}

bool Mxdrv::LoadMDX(void)
{
	int mdx_size;
	int pdx_size;

	int i;
	int mdx_top;

	for (i = 0; i < mdx_file_size; i++) {
		if (mdx_file[i] == 0x0d) {
			i++;
			break;
		}
	}
	if (i == mdx_file_size) return false;
	for (; i < mdx_file_size; i++) {
		if (mdx_file[i] == 0x1a) {
			i++;
			break;
		}
	}
	if (i == mdx_file_size) return false;
	for (; i < mdx_file_size; i++) {
		if (mdx_file[i] == 0x00) {
			i++;
			mdx_top = i;
			break;
		}
	}
	if (i == mdx_file_size) return false;

	if (mdx_file[mdx_top+4] == 'L' &&
		mdx_file[mdx_top+5] == 'Z' &&
		mdx_file[mdx_top+6] == 'X' &&
		mdx_file[mdx_top+7] == ' ') {
		// LZMDX
		mdx_size = ::ReadDWORD(&mdx_file[mdx_top + 0x12]);
	} else {
		// Normal MDX
		mdx_size = mdx_file_size - mdx_top;
	}

	m_mdx_adr = 0x10000;
	m_mdx_bank_size = ((mdx_size + 6) + 0xffff) & 0xff0000;
	mdx = new BYTE[m_mdx_bank_size];
	memset(mdx, 0, m_mdx_bank_size);
	memcpy(&mdx[6], &mdx_file[mdx_top], mdx_file_size - mdx_top);


	m_pdx_adr = m_mdx_adr + m_mdx_bank_size;
	m_pdx_bank_size = 0;

	WORD pdx_flag;

	if (pdx_file_size != 0) {
		if (pdx_file[4] == 'L' &&
			pdx_file[5] == 'Z' &&
			pdx_file[6] == 'X' &&
			pdx_file[7] == ' ') {
			// LZMDX
			pdx_size = ::ReadDWORD(&pdx_file[0x12]);
		} else {
			// Normal PDX
			pdx_size = pdx_file_size;
		}

		pdx_flag = 0x0000;
		m_pdx_bank_size = ((pdx_size + 6) + 0xffff) & 0xff0000;
		pdx = new BYTE[m_pdx_bank_size];
		memset(pdx, 0, m_pdx_bank_size);
		memcpy(&pdx[6], pdx_file, pdx_file_size);
	} else {
		pdx_flag = 0xffff;
	}

	WriteWORD(&mdx[0], 0x0000);
	WriteWORD(&mdx[2], pdx_flag);
	WriteWORD(&mdx[4], 0x0006);

	WriteDWORD(&ram[MDX_ADR], m_mdx_adr);
	WriteDWORD(&ram[MDX_SIZE], mdx_file_size - mdx_top + 6);
	WriteDWORD(&ram[MDX_BANK_SIZE], m_mdx_bank_size);
	ram[MDX_LOADED] = 0xff;

	if (m_pdx_bank_size != 0) {
		WriteDWORD(&pdx[0], 0x00000000);
		WriteWORD(&pdx[4], 0x0006);
		WriteDWORD(&ram[PDX_ADR], m_pdx_adr);
		WriteDWORD(&ram[PDX_SIZE], pdx_file_size + 6);
		WriteDWORD(&ram[PDX_BANK_SIZE], m_pdx_bank_size);
		ram[PDX_LOADED] = 0xff;
	} else {
		WriteDWORD(&ram[PDX_ADR], 0);
		WriteDWORD(&ram[PDX_SIZE], 0);
		WriteDWORD(&ram[PDX_BANK_SIZE], 0);
		ram[PDX_LOADED] = 0x00;
	}

	return true;
}

static string GetTitle(BYTE *_mdx, int _size)
{
	int i;

	for (i = 0; i < _size; i++) {
		if (_mdx[i] == 0x0d) {
			_mdx[i] = 0;
			string title = string((char *)_mdx);
			_mdx[i] = 0x0d;
			return title;
		}
	}

	return string("");
}

static string GetPdxName(BYTE *_mdx, int _size)
{
	int i, j;

	for (i = 0; i < _size; i++) {
		if (_mdx[i] == 0x0d) {
			i++;
			for (j = i; j < _size; j++) {
				if (_mdx[j] == 0x1a) {
					j++;
					return string((char *)(&_mdx[j]));
				}
			}
			return string("");
		}
	}

	return string("");
}

bool Mxdrv::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();
	const ssConfig &config = sdm->GetConfig();

	int opm_mix = 0xc0;
	int pcm_mix = 0xf0;
	int viewadr = 0;

	opm_mix = _config->GetOption("opm_mix", opm_mix);
	pcm_mix = _config->GetOption("pcm_mix", pcm_mix);

	{
		ssFile file;
		file.Open(string("mxdrv.bin"));
		file.Read(ram, sizeof(ram));
	}

	string title;
	if (_config->IsSingleFile()) {
		ssFile file;
		if (file.Open(_config->archive)) {
			string pdx_name;

			mdx_file_size = file.GetSize();
			mdx_file = new BYTE[mdx_file_size];
			file.Read(mdx_file, mdx_file_size);

			title = GetTitle(mdx_file, mdx_file_size);
			pdx_name = GetPdxName(mdx_file, mdx_file_size);

			if (pdx_name == "") {
				pdx_file_size = 0;
			} else {
				{
					char path[_MAX_PATH];
					char drive[_MAX_DRIVE];
					char dir[_MAX_DIR];
					char fname[_MAX_FNAME];
					char ext[_MAX_EXT];
					_splitpath(_config->archive.c_str(), drive, dir, fname, ext);
					_makepath(path, drive, dir, NULL, NULL);
					file.AddSearchPath(string(path));
					file.AddSearchPath(config.pdxpath);
				}
				{
					char pdx_fname[_MAX_PATH];
					char drive[_MAX_DRIVE];
					char dir[_MAX_DIR];
					char fname[_MAX_FNAME];
					char ext[_MAX_EXT];
					_splitpath(pdx_name.c_str(), drive, dir, fname, ext);
					if (ext[0] != '.') {
						strcpy(ext, ".pdx");
					}
					_makepath(pdx_fname, NULL, NULL, fname, ext);
					pdx_name = pdx_fname;
				}

				if (file.Open(pdx_name)) {
					pdx_file_size = file.GetSize();
					pdx_file = new BYTE[pdx_file_size];
					file.Read(pdx_file, pdx_file_size);
				} else {
					pdx_file_size = 0;
				}
			}

			LoadMDX();
		} else {
			return false;
		}
	} else {
		return false;
	}

	if (title == "") title = "PLAY";
	_config->AddTitle(1, title);
	_config->AddTitle(2, string("STOP"));

	m68000_init();
	cpu_init_memmap24();

	cpu_map_fetch(0x000000, 0x00ffff, &ram[0x000000]);
	cpu_map_read(0x000000, 0x00ffff, &ram[0x000000]);
	cpu_map_write(0x000000, 0x00ffff, &ram[0x000000]);

	cpu_map_fetch(m_mdx_adr, m_mdx_adr + m_mdx_bank_size - 1, mdx);
	cpu_map_read(m_mdx_adr, m_mdx_adr + m_mdx_bank_size - 1, mdx);
	cpu_map_write(m_mdx_adr, m_mdx_adr + m_mdx_bank_size - 1, mdx);

	if (m_pdx_bank_size != 0) {
		cpu_map_fetch(m_pdx_adr, m_pdx_adr + m_pdx_bank_size - 1, pdx);
		cpu_map_read(m_pdx_adr, m_pdx_adr + m_pdx_bank_size - 1, pdx);
		cpu_map_write(m_pdx_adr, m_pdx_adr + m_pdx_bank_size - 1, pdx);
	}

	cpu_add_read(0xe00000, 0xefffff, CPU_MAP_HANDLED, sReadDev);
	cpu_add_write(0xe00000, 0xefffff, CPU_MAP_HANDLED, sWriteDev);

	cpu_end_memmap();

	m68000_reset(0);

	m_YM2151->Initialize(4000000);
	m_YM2151->SetVolume(0, 0, opm_mix);
	sdm->RegisterSoundChip(m_YM2151);

	m_ADPCM->Initialize();
	m_ADPCM->SetVolume(0, 0, pcm_mix);
	sdm->RegisterSoundChip(m_ADPCM);

	// ダンプアドレスの設定
	m_MemDump->SetAddress(mdx);
	//m_MemDump->SetAddress(pdx);
	//m_MemDump->SetAddress(&ram[0x2690+800]);
	//m_MemDump->SetAddress(&ram[0]);
	//sdm->RegisterSoundChip(m_MemDump);

	m68000_emulate(50000);

	return true;
}

void Mxdrv::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2151->TimerAOver();
		break;

	case FM_TIMER_B:
		m_YM2151->TimerBOver();
		m68000_raise_IRQ(MC68000_IRQ_1);
		m68000_lower_IRQ(MC68000_IRQ_1);
		break;
	default:
		break;
	}
	Unlock();
}

bool Mxdrv::Play(DWORD _code)
{
	Lock();

	m_code = _code;

	Unlock();

	return true;
}

bool Mxdrv::Stop()
{
	return true;
}

// ドライバの登録
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "x68k") {
		if (_config->driver_subtype == "mxdrv") {
			return Mxdrv::Instance(Mxdrv::TYPE_GENERIC);
		}
	}
	return NULL;
}

static ssDriverRegister x68k(string("x68k"), CreateDriver, NULL);
