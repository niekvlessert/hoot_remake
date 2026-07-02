#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssYM2203.h"

#include "ssUnZip.h"

#include "raze/raze.h"

class Mucom88Driver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_YS,
		TYPE_YS2,
		TYPE_YS3,
		TYPE_YK2,
	};
	enum {
		SUBTYPE_ALGANA = 0,
	};
	static Mucom88Driver *Instance(int _type = TYPE_GENERIC);
private:
	static Mucom88Driver *m_Instance;
protected:
	Mucom88Driver(int _type = TYPE_GENERIC);
public:
	virtual ~Mucom88Driver();

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
		BGM_SIZE = 8 * 1024,
		PROG_SIZE = 32 * 1024,
		PLAY_FLAG = 0xc010,
		PLAY_CODE = 0xc011,
		LOAD_FLAG = 0xc012,
		STOP_ADR = 0xc012,
		PLAY_ADR = 0xc014,
		YS_PLAY_FLAG = 0xc010,
		YS_PLAY_CODE = 0xc011,
		YS_STOP_ADR = 0xc012,
		YS_PLAY_ADR = 0xc014,
		YS_IREG = 0xc016,
		YS3_PLAY_FLAG = 0xf010,
		YS3_PLAY_CODE = 0xf011,
		YS3_PLAY_BANK = 0x52ec,
		YS3_LOAD_FLAG = 0xf012,
	};
	enum {
		YK2_TRIGGER = 0x10,
		YK2_COMMAND,
		YK2_IDLING,

		YK2_TRIGGER_FLAG = 0x00ff,
	};

	int m_type;

	ssYM2203 *m_YM2203;
	ssTimer *m_Timer;

	BYTE ram[0x10000];
	BYTE ioport[0x100];

	bool bgm_flag[128];
	BYTE bgm[BGM_SIZE * 128];
	BYTE prog[PROG_SIZE * 4];
};

Mucom88Driver *Mucom88Driver::m_Instance = NULL;

Mucom88Driver *Mucom88Driver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new Mucom88Driver(_type);
	}
	return m_Instance;
}

Mucom88Driver::Mucom88Driver(int _type)
{
	m_type = _type;

	m_YM2203 = new ssYM2203(FM_TIMER_A, FM_TIMER_B);
	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/600.0);
	//m_Timer->SetInterval(1.0/60.0);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));
	memset(bgm, 0, sizeof(bgm));

	for (int b = 0; b < 128; b++) {
		bgm_flag[b] = false;
	}

	int p;
	for (p = 0; p <= 0x0e; p++) {
		ioport[p] = 0xff;
	}
}

Mucom88Driver::~Mucom88Driver()
{
	if (m_YM2203) {
		delete m_YM2203;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	m_Instance = NULL;
}

void Mucom88Driver::WriteCtrl(WORD _adr, BYTE _data)
{
	ram[_adr] = _data;
}

BYTE Mucom88Driver::ReadCtrl(WORD _adr)
{
	return 0xff;
}

void Mucom88Driver::sWriteCtrl(WORD _adr, BYTE _data)
{
	Mucom88Driver::m_Instance->WriteCtrl(_adr, _data);
}

BYTE Mucom88Driver::sReadCtrl(WORD _adr)
{
	return Mucom88Driver::m_Instance->ReadCtrl(_adr);
}

BYTE Mucom88Driver::ReadPort(WORD _port)
{
	const BYTE port = _port;
	switch (port) {
	case 0x44:
	case 0x45:
		return m_YM2203->Read(_port);
		break;
	case YK2_TRIGGER:
		ioport[port] = ram[YK2_TRIGGER_FLAG];
		ram[YK2_TRIGGER_FLAG] = 0;
		break;
	case YK2_IDLING:
		if (m_type == TYPE_YK2) {
			z80_skip_idle();
		}
		break;
	default:
		TRACE("%04x port-read(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, ioport[port]);
		break;
	}
	return ioport[port];
}

void Mucom88Driver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
	case 0x00:
		if (m_type == TYPE_GENERIC) {
			if (bgm_flag[_data]) {
				memcpy(&ram[(ram[0x5d]<<8)+ram[0x5c]], &bgm[BGM_SIZE * _data], BGM_SIZE);
				ram[LOAD_FLAG] = 0xff;
			}
		} else if (m_type == TYPE_YS3) {
			if (bgm_flag[_data]) {
				memcpy(&ram[0xd200], &bgm[BGM_SIZE * _data], 0x1000);
				ram[YS3_LOAD_FLAG] = 0xff;
			}
		}
		break;
	case 0x44:
	case 0x45:
		m_YM2203->Write(_port, _data);
		TRACE("OPN(%02x) : %02x\n", port, _data);
		break;
	case 0xe4:
		z80_lower_IRQ();
		TRACE("lower IRQ (E4)\n");
		break;
	case YK2_COMMAND:
		if (m_type == TYPE_YK2) {
			if (bgm_flag[_data]) {
				memcpy(&ram[0xc000], &bgm[BGM_SIZE * _data], 0x1000);
			}
		}
		break;
	default:
		TRACE("%04x port-write(%02x) : %02x\n",
			z80_get_reg(Z80_REG_PC), port, _data);
		break;
	}
	ioport[port] = _data;
}

BYTE Mucom88Driver::sReadPort(WORD _port)
{
	return Mucom88Driver::m_Instance->ReadPort(_port);
}

void Mucom88Driver::sWritePort(WORD _port, BYTE _data)
{
	Mucom88Driver::m_Instance->WritePort(_port, _data);
}

void Mucom88Driver::Execute(double _second)
{
	Lock();
	z80_emulate((int)(4000000.0 * _second));
	//z80_emulate((int)(8000000.0 * _second));
	Unlock();
}

bool Mucom88Driver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	{
		ssUnZip zip;

		zip.Open(_config->archive);

		ssDriverConfig::ssRomList::const_iterator i;
		for (i = _config->romlist.begin(); i != _config->romlist.end(); i++) {
			if (i->type == "code") {
				zip.Load(i->filename, ram + i->offset, 0x4000);
			} if (i->type == "prog") {
				zip.Load(i->filename, prog + i->offset * PROG_SIZE, PROG_SIZE);
			} else if (i->type == "bgm") {
				bgm_flag[i->offset] = true;
				zip.Load(i->filename, bgm + i->offset * BGM_SIZE, BGM_SIZE);
			}
		}
		zip.Close();

		if (m_type == TYPE_GENERIC && !bgm_flag[0]) {
			memcpy(&bgm[0x0000], &ram[0x6800], BGM_SIZE);
			bgm_flag[0] = true;
		}
	}

	z80_init_memmap();

	z80_map_fetch(0x0000, 0xffff, &ram[0x0000]);

	z80_add_read(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_add_write(0x0000, 0xffff, Z80_MAP_DIRECT, &ram[0x0000]);

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_reset();
	if (m_type == TYPE_YS3) {
		z80_set_reg(Z80_REG_PC, 0xf000);
	} else if (m_type == TYPE_YK2) {
		z80_set_reg(Z80_REG_PC, 0x0000);
	} else {
		z80_set_reg(Z80_REG_PC, 0xc000);
	}

	m_YM2203->Initialize(3993600);
	m_YM2203->SetVolume(0, 0, 0x100);
	m_YM2203->SetVolume(1, 0, 0x50);
	sdm->RegisterSoundChip(m_YM2203);

	z80_emulate(500);

	return true;
}

void Mucom88Driver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
	case FM_TIMER_A:
		m_YM2203->TimerAOver();
#if 0
		if ((ioport[0x32] & 0x80) == 0) {
			z80_raise_IRQ(8);
			z80_lower_IRQ();
		}
#endif
		//TRACE("A Tick:%d\n", GetTickCount());
		break;
	case FM_TIMER_B:
		m_YM2203->TimerBOver();
		if ((ioport[0x32] & 0x80) == 0) {
#if 0
			char s[64];
			sprintf(s, "INT Timer-B (%04X)\n", z80_get_reg(Z80_REG_PC));
			OutputDebugString(s);
#endif
			z80_raise_IRQ(8);
			//z80_lower_IRQ();
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

static struct {
	WORD stop;
	WORD play;
	WORD track;
	WORD bgm;
	WORD prog_adr;
} ys_adr_tbl[4] = {
	{0x9c4f, 0x9970, 0x944b, 0x8000, 0x9000},
	{0x2bc3, 0x293e, 0x2481, 0x4d00, 0x0100},
	{0x2c62, 0x2986, 0x2461, 0x4d00, 0x0100},
	{0x5058, 0x4d70, 0x47a1, 0x6000, 0x4000}
};

static struct {
	WORD stop;
	WORD play;
	WORD track;
	WORD bgm;
} ys2_adr_tbl[3] = {
	{0x2d9e, 0x2a52, 0x25d6, 0x3000},
	{0x2d02, 0x29b3, 0x4b00, 0x4d00},
	{0x18d5, 0x15fc, 0x1197, 0x2000}
};

bool Mucom88Driver::Play(DWORD _code)
{
	Lock();
	if (m_type == TYPE_GENERIC) {
		switch (_code >> 8) {
		case 0x00:
			ram[PLAY_FLAG] = 0x01;
			ram[PLAY_CODE] = _code;
			break;
		case 0x01:
			ram[0x005a] = _code;
			break;
		}
	} else if (m_type == TYPE_YS) {
		BYTE p = _code >> 12;
		BYTE code = _code >> 4;
		BYTE track = _code & 0x0f;
		WORD *stop = (WORD *)(&ram[YS_STOP_ADR]);
		WORD *play = (WORD *)(&ram[YS_PLAY_ADR]);
		*stop = ys_adr_tbl[p].stop;
		*play = ys_adr_tbl[p].play;
		if (p == 0) {
			memcpy(&ram[ys_adr_tbl[p].prog_adr], &prog[p * PROG_SIZE], 0x3000);
			memcpy(&ram[ys_adr_tbl[p].bgm], &bgm[code * BGM_SIZE], 0x1000);
		} else {
			memcpy(&ram[ys_adr_tbl[p].prog_adr], &prog[p * PROG_SIZE], PROG_SIZE);
			memcpy(&ram[ys_adr_tbl[p].bgm], &bgm[code * BGM_SIZE], BGM_SIZE);
		}
		ram[ys_adr_tbl[p].track] = track;
		ram[YS_PLAY_FLAG] = 0x01;
		ram[YS_IREG] = ys_adr_tbl[p].prog_adr >> 8;
		z80_reset();
		z80_set_reg(Z80_REG_PC, 0xc000);
		z80_emulate(1000);
	} else if (m_type == TYPE_YS2) {
		BYTE p = _code >> 12;
		BYTE code = _code >> 4;
		BYTE track = _code & 0x0f;
		WORD *stop = (WORD *)(&ram[STOP_ADR]);
		WORD *play = (WORD *)(&ram[PLAY_ADR]);
		memcpy(&ram[0x0100], &prog[p * PROG_SIZE], PROG_SIZE);
		*stop = ys2_adr_tbl[p].stop;
		*play = ys2_adr_tbl[p].play;
		memcpy(&ram[ys2_adr_tbl[p].bgm], &bgm[code * BGM_SIZE], BGM_SIZE);
		ram[ys2_adr_tbl[p].track] = track;
		ram[PLAY_FLAG] = 0x01;
		z80_reset();
		z80_set_reg(Z80_REG_PC, 0xc000);
		z80_emulate(1000);
	} else if (m_type == TYPE_YS3) {
		ram[YS3_PLAY_FLAG] = 0x01;
		ram[YS3_PLAY_CODE] = _code;
		ram[YS3_PLAY_BANK] = _code >> 8;
	} else if (m_type == TYPE_YK2) {
		ram[YK2_TRIGGER_FLAG] = _code;
	}
	Unlock();
	return true;
}

bool Mucom88Driver::Stop()
{
	Lock();
	if (m_type == TYPE_GENERIC) {
		ram[PLAY_FLAG] = 0xff;
	} else if (m_type == TYPE_YS) {
		ram[PLAY_FLAG] = 0x00;
		z80_reset();
		z80_set_reg(Z80_REG_PC, 0xc000);
		z80_emulate(1000);
	} else if (m_type == TYPE_YS2) {
		ram[PLAY_FLAG] = 0x00;
		z80_reset();
		z80_set_reg(Z80_REG_PC, 0xc000);
		z80_emulate(1000);
	} else if (m_type == TYPE_YS3) {
		ram[YS3_PLAY_FLAG] = 0xff;
	} else if (m_type == TYPE_YK2) {
		ram[YK2_TRIGGER_FLAG] = 0xff;
	}
	Unlock();
	return true;
}


// ドライバ情報
static const ssDriverDescription Description[] =
{
	{
		"mucom88",
		"generic",
		{"MUCOM88 (PC-88/PC-98)", "SORCERIAN", "etc..."},
		{
			// Files
			{"code",	{"main memory"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	{
		"mucom88",
		"ys",
		{"Ys (PC-88)"},
		{
			// Files
			{"code",	{"main memory"}},
			{"prog",	{"driver program"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	{
		"mucom88",
		"ys2",
		{"Ys-II (PC-88)"},
		{
			// Files
			{"code",	{"main memory"}},
			{"prog",	{"driver program"}},
			{"bgm",		{"BGM data", "offset means BGM number"}},
		},
	},
	{
		"mucom88",
		"ys3",
		{"Ys-3 (PC-88)"},
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
		if (_config->driver_subtype == "generic") {
			return Mucom88Driver::Instance(Mucom88Driver::TYPE_GENERIC);
		} else if (_config->driver_subtype == "ys") {
			return Mucom88Driver::Instance(Mucom88Driver::TYPE_YS);
		} else if (_config->driver_subtype == "ys2") {
			return Mucom88Driver::Instance(Mucom88Driver::TYPE_YS2);
		} else if (_config->driver_subtype == "ys3") {
			return Mucom88Driver::Instance(Mucom88Driver::TYPE_YS3);
		} else if (_config->driver_subtype == "yk-2opn") {
			return Mucom88Driver::Instance(Mucom88Driver::TYPE_YK2);
		}
	}
	return NULL;
}

static ssDriverRegister mucom88(string("mucom88"), CreateDriver, Description);
