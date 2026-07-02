//
// MSX [SCC] ドライバ
//
// KONAMI ならこれですべて鳴る…はずぉ
//

#include "StdAfx.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"
#include "ssDriverConfig.h"
#include "ssDriverDescription.h"
#include "ssDriverRegister.h"
#include "sound/ssAY8910.h"
#include "sound/ss051649.h"

#include "ssUnZip.h"

#include "raze/raze.h"

// ROM/DISKIMAGE 用 IPL
static BYTE ipl [] = {
// +0    +1    +2    +3    +4    +5    +6    +7    +8    +9    +A    +B    +C    +D    +E    +F
 0xed, 0x56, 0xc3, 0x07, 0x00, 0x00, 0x00, 0x31, 0x80, 0xf3, 0x3a, 0x05, 0x00, 0xb7, 0x28, 0xfa,
 0xcd, 0x30, 0x00, 0xf3, 0xaf, 0x32, 0x05, 0x00, 0x3a, 0x06, 0x00, 0xcd, 0x00, 0x60, 0xfb, 0x18,
 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 0xc9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf3, 0xf5, 0xc5, 0xd5, 0xe5, 0xdd, 0xe5, 0xfd,
 0xe5, 0xcd, 0x22, 0x64, 0xfd, 0xe1, 0xdd, 0xe1, 0xe1, 0xd1, 0xc1, 0xf1, 0xfb, 0xc9, 0xff, 0xff,
// 0x50, for SD SNATCHER
 0xcd, 0x00, 0x44, 0xaf, 0x32, 0x40, 0x42, 0xcd, 0xd7, 0x4e, 0xc3, 0x00, 0x00
};

// PSG R/W 入口
static BYTE ipl2nd [] = {
 0xc3, 0x02, 0x11, 0xc3, 0x0e, 0x11
};

// PSG R/W 本体
static BYTE ipl3rd [] = {
 0xf3, 0xd3, 0xa0, 0xf5, 0x7b, 0xd3, 0xa1, 0xfb, 0xf1, 0xc9, 0x00, 0x00,
 0xd3, 0xa0, 0xdb, 0xa2, 0xc9
};

// SD SNATCHER 用マッピング情報
static BYTE sdsnatcher_map[] = {
 0x00, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
 0x02, 0x09, 0x09, 0x03, 0x03, 0x08, 0x01, 0x07, 0x0c, 0x11, 0x0c, 0x00, 0x00, 0x10, 0x01, 0x01,
 0x0c, 0x0d, 0x03, 0x03, 0x0e, 0x0e, 0x01, 0x12, 0x0f, 0x10, 0x13, 0x11, 0x12, 0x12, 0x00, 0x09,
 0x0b, 0x0e, 0x09, 0x0f, 0x03, 0x0a, 0x0a, 0x0a, 0x0a, 0x06, 0x0c, 0x03, 0x11, 0x09, 0x04, 0x05,
 0x01, 0x03, 0x09, 0x0b, 0x0c, 0x0d, 0x13, 0x13, 0x13, 0x14, 0x14, 0x14, 0x02, 0x08, 0x08, 0x14,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x09, 0x00, 0x02, 0x02, 0x0a,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
};


//
// クラスの定義
//
class SccDriver : public ssSoundDriver
{
public:
	enum {
		TYPE_GENERIC = 0,
		TYPE_SCC,
		TYPE_PSG,
	};
	static SccDriver *Instance(int _type = TYPE_GENERIC);
private:
	static SccDriver *m_Instance;
protected:
	SccDriver(int _type = TYPE_GENERIC);
public:
	virtual ~SccDriver();

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
		TIMER_INT,
	};
	enum {
		PLAY_FLAG = 0x0005,
		PLAY_CODE = 0x0006,
	};

	int m_type;
	
	ssAY8910 *m_AY8910;
	ss051649 *m_051649;
	ssTimer *m_Timer;

	int mapadr;							// ROM 張り付けアドレス
	int mapofs;							// ROM 張り付けるオフセット
	int init_adr;						// 初期化実行アドレス
	char sd_snatcher;					// SD SNATCHER モードにするか？(0:ROM、<>0:SD SNATCHER)

	char curbank;						// 現在マッピング中のバンク

	char dummy;

	BYTE ram[0x10000];
	BYTE rom[0x80000];
	BYTE ioport[0x100];
};

SccDriver *SccDriver::m_Instance = NULL;

//
// インスタンス生成
//
SccDriver *SccDriver::Instance(int _type)
{
	if (m_Instance == NULL) {
		m_Instance = new SccDriver(_type);
	}
	return m_Instance;
}

//
// コンストラクタ
//
SccDriver::SccDriver(int _type)
{
	m_type = _type;

	m_AY8910 = new ssAY8910();
	m_051649 = NULL;

	m_Timer = new ssTimer(TIMER_INT);
	m_Timer->SetInterval(1.0/60.0);

	memset(ram, 0, sizeof(ram));
	memset(ioport, 0, sizeof(ioport));

	curbank = -1;
}

//
// デストラクタ
//
SccDriver::~SccDriver()
{
	if (m_051649) {
		delete m_051649;
	}
	if (m_AY8910) {
		delete m_AY8910;
	}
	if (m_Timer) {
		delete m_Timer;
	}
	m_Instance = NULL;
}

//
// Z80 エリアに書き込みがあった場合のハンドラ
// 実際ここが呼び出される
//
void SccDriver::WriteCtrl(WORD _adr, BYTE _data)
{
	switch(_adr) {
		// マッピング変更
		case 0x6000:
		case 0x8000:
		case 0xa000:
			break;

		case 0x9000:
			// 0x3f : SCC アクセス許可
			if (m_051649 && (_data == 0x3f)) {
				m_051649->SetSccEnable();

			// それ以外 : SCC アクセス禁止
			} else {
				if (m_051649) {
					m_051649->SetSccDisable();
				}
			}
			break;

		case 0xb000:
			// バンクの異なる場合は再度マッピングを行う
			// curbank はコード指定時に破壊しているため、一度は必ずここを通る
			if (curbank != _data) {
				curbank = _data;
				z80_map_read(0xa000, 0xbfff, &rom[_data * 0x2000]);
			}
			break;

		// その他のアドレス
		default:
			if (m_051649 && m_051649->GetSccAccessStatus()) {
				m_051649->Write(_adr, _data);
			}
			break;
	}
}

//
// Z80 エリアから読み出しがあった場合のハンドラ
// 実際ここが呼び出される
//
BYTE SccDriver::ReadCtrl(WORD _adr)
{
	const BYTE adr = _adr >> 8;
	BYTE r;

	switch(adr) {
		// SCC のワーク内容を返す
		case 0x98:
		case 0xb8:
			if (m_051649 && (_adr & 0x7f <= 0x7f)) {
				r = m_051649->Read(_adr);
			} else {
				r = 0xff;
			}
			break;

		default:
			r = 0xff;
			break;
	}

	return r;
}

//
// Z80 エリアに書き込みがあった場合のハンドラ
//
void SccDriver::sWriteCtrl(WORD _adr, BYTE _data)
{
	SccDriver::m_Instance->WriteCtrl(_adr, _data);
}

//
// Z80 エリアから読み出しがあった場合のハンドラ
//
BYTE SccDriver::sReadCtrl(WORD _adr)
{
	return SccDriver::m_Instance->ReadCtrl(_adr);
}

//
// ＳＳＧポート読みだし
//
BYTE SccDriver::ReadPort(WORD _port)
{
	const BYTE port = _port;

	switch (port) {
		case 0xa0:
		case 0xa1:
			return m_AY8910->Read();
		break;
	}
	return ioport[port];
}

//
// ＳＳＧポート書き込み
//
void SccDriver::WritePort(WORD _port, BYTE _data)
{
	const BYTE port = _port;
	switch (port) {
		case 0xa0:
		case 0xa1:
			m_AY8910->Write(_port, _data);
			break;

		default:
			break;
	}
	ioport[port] = _data;
}

BYTE SccDriver::sReadPort(WORD _port)
{
	return SccDriver::m_Instance->ReadPort(_port);
}

void SccDriver::sWritePort(WORD _port, BYTE _data)
{
	SccDriver::m_Instance->WritePort(_port, _data);
}

//
// エミュレーション実行
//
void SccDriver::Execute(double _second)
{
	Lock();

	z80_emulate((int)(3579545.0 * _second));

	Unlock();
}

//
// ドライバ読み込みと初期化
//
bool SccDriver::Initialize(ssDriverConfig *_config)
{
	ssSoundDriverManager *sdm = ssSoundDriverManager::Instance();

	mapadr = 0x6000;				// ROM 張り付けアドレス、てきとー (ROM のみ)
	mapofs = 0x10000;				// ROM 張り付けるオフセット、てきとー (ROM のみ)
	init_adr = 0x0000;				// 初期化実行アドレス

	sd_snatcher = 0;				// SD SNATCHER モードにしない

	int cmd_adr = 0x6000;			// コマンド解釈アドレス
	int irq_adr = 0x6422;			// サウンド割り込み処理アドレス
	int psg_mix = 0xa5;				// ＳＳＧボリューム
	int scc_mix = 0x84;				// ＳＣＣボリューム

	// SCC の場合、SCC クラス生成
	if (m_type == TYPE_SCC) {
		m_051649 = new ss051649();
	}

	// 各種アドレス、ROMオフセットの取得
	mapadr = _config->GetOption("mapadr", mapadr);
	mapofs = _config->GetOption("mapofs", mapofs);

	init_adr = _config->GetOption("init_adr", init_adr);
	cmd_adr = _config->GetOption("cmd_adr", cmd_adr);
	irq_adr = _config->GetOption("irq_adr", irq_adr);

	psg_mix = _config->GetOption("psg_mix", psg_mix);
	scc_mix = _config->GetOption("scc_mix", scc_mix);

	sd_snatcher = _config->GetOption("diskimage", sd_snatcher);
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

	//
	// メモリマッピング
	//
	z80_init_memmap();

	if (sd_snatcher == 0) {

		// ROM 指定時
		// 命令マッピング
		z80_map_fetch(0x0000, 0x3fff, &ram[0x0000]);
		z80_map_fetch(0x4000, 0x5fff, &rom[0x0000]);		// ROM 先頭からを 4000h- へ
		z80_map_fetch(mapadr, 0xbfff, &rom[mapofs]);		// ROM 指定位置を 指定アドレス以降へ

		// ROM 領域マッピング
		z80_add_read(0x0000, 0x3fff, Z80_MAP_DIRECT, &ram[0x0000]);
		z80_add_read(0x4000, 0x5fff, Z80_MAP_DIRECT, &rom[0x0000]);
		z80_map_read(mapadr, 0xbfff, &rom[mapofs]);
		z80_add_read(0xc000, 0xffff, Z80_MAP_DIRECT, &ram[0xc000]);

		z80_add_write(0x0000, 0x3fff, Z80_MAP_DIRECT, &ram[0x0000]);
//		z80_add_write(0x4000, 0x5fff, Z80_MAP_DIRECT, &ram[0x4000]);
//		z80_map_write(mapadr, 0xbfff, &rom[mapofs]);
		z80_add_write(0xc000, 0xffff, Z80_MAP_DIRECT, &ram[0xc000]);

	} else {

		// SD SNATCHER 指定時(暫定)
		// 命令マッピング
		z80_map_fetch(0x0000, 0x3fff, &ram[0x0000]);
		z80_map_fetch(0x4000, 0x9fff, &rom[0x0000]);		// イメージ先頭からを 4000h- へ

		// ROM 領域マッピング
		z80_add_read(0x0000, 0x3fff, Z80_MAP_DIRECT, &ram[0x0000]);
		z80_add_read(0x4000, 0x9fff, Z80_MAP_DIRECT, &rom[0x0000]);
		z80_map_read(0xa000, 0xbfff, &ram[0xa000]);
		z80_add_read(0xc000, 0xffff, Z80_MAP_DIRECT, &ram[0xc000]);

		z80_add_write(0x0000, 0x3fff, Z80_MAP_DIRECT, &ram[0x0000]);
		z80_add_write(0x4000, 0x9fff, Z80_MAP_DIRECT, &rom[0x0000]);
		z80_map_write(0xa000, 0xbfff, &ram[0xa000]);
		z80_add_write(0xc000, 0xffff, Z80_MAP_DIRECT, &ram[0xc000]);

		// 最初のマッピング状態を設定
		memcpy(&ram[0xa000], &rom[0x6000], 0x2000);
	}

	// IPL の転送
	memcpy(&ram[0x0000], ipl, sizeof(ipl));			// IPL
	memcpy(&ram[0x0093], ipl2nd, sizeof(ipl2nd));	// PSG W/R ENTRY
	memcpy(&ram[0x1102], ipl3rd, sizeof(ipl3rd));	// PSG W/R MAIN ROUTINE

	// SD SNATCHER の場合、パッチ当てを行う
	if (sd_snatcher) {
		rom[0x171f] = 0xc9;		// ret
		rom[0x172c] = 0xc9;		// ret
	}

	// コマンド解釈、割り込みアドレスを設定(Z80側)
	if (init_adr) {
		ram[0x0011] = init_adr & 0xff;
		ram[0x0012] = init_adr >> 8;
	}
	ram[0x001c] = cmd_adr & 0xff;
	ram[0x001d] = cmd_adr >> 8;
	ram[0x0042] = irq_adr & 0xff;
	ram[0x0043] = irq_adr >> 8;

	// ハンドラ定義
	z80_add_write(0x9000, 0x9000, Z80_MAP_HANDLED, sWriteCtrl);		// for CHANGE BANK & SCC Access
	z80_add_write(0xb000, 0xb000, Z80_MAP_HANDLED, sWriteCtrl);		// for CHANGE BANK

	z80_add_write(0x9800, 0x98ff, Z80_MAP_HANDLED, sWriteCtrl);		// for SCC  port
	z80_add_write(0xb800, 0xb8af, Z80_MAP_HANDLED, sWriteCtrl);		// for SCC+ port

	z80_add_write(0x6000, 0x6000, Z80_MAP_HANDLED, sWriteCtrl);		// for BANK Change(through)
	z80_add_write(0x8000, 0x8000, Z80_MAP_HANDLED, sWriteCtrl);		// for BANK Change(through)
	z80_add_write(0xa000, 0xa000, Z80_MAP_HANDLED, sWriteCtrl);		// for BANK Change(through)

	z80_end_memmap();

	z80_set_in(&sReadPort);
	z80_set_out(&sWritePort);

	z80_reset();

	// SP 初期化
	z80_set_reg(Z80_REG_SP, 0xfffe);

	// サウンドエンジン初期設定
	m_AY8910->Initialize(3579545 / 2);
	m_AY8910->SetVolume(0, 0, psg_mix);
	sdm->RegisterSoundChip(m_AY8910);

	if (m_051649) {
		m_051649->Initialize(3579545 / 2);
		m_051649->SetVolume(0, 0, scc_mix);
		sdm->RegisterSoundChip(m_051649);
	}

	z80_emulate(10000);

	return true;
}

//
// Z80 割り込み発生時
//
void SccDriver::Interrupt(int _id)
{
	Lock();
	switch (_id) {
		case TIMER_INT:
			z80_raise_IRQ(0xff);
			z80_lower_IRQ();
			break;

		default:
			break;
	}
	Unlock();
}

//
// 演奏（コードセット）
//
bool SccDriver::Play(DWORD _code)
{
	BYTE code = _code;


	Lock();

	z80_reset();

	// RAM 領域初期化
	memset(&ram[0xc000], 0, 0x4000);
	if (m_051649) {
		m_051649->InitSccWork();
	}

	// ROM の場合の再生
	if (sd_snatcher == 0) {
		// SCC の場合、マッピング状態初期化
		// 遅いけどやる。:-)
		if (m_type == TYPE_SCC) {
			z80_map_fetch(mapadr, 0xbfff, &rom[mapofs]);
			z80_map_read(mapadr, 0xbfff, &rom[mapofs]);
		}

		// コード書き込み
		ram[PLAY_FLAG] = 0x01;
		ram[PLAY_CODE] = code;
		curbank = -1;

		// Z80 エミュレータの初期化
		z80_set_reg(Z80_REG_SP, 0xfffe);
//		z80_set_reg(Z80_REG_PC, 0x0000);

	// SD SNATCHER の場合
	} else {
		BYTE *src = rom + 0x6000;

		if (code > 0x80) {
			code = 0x00;
		}
		memcpy(&ram[0xa000], src + (sdsnatcher_map[code] * 0x2000), 0x2000);

		// コード書き込み
		ram[PLAY_FLAG] = 0x01;
		ram[PLAY_CODE] = code;

		// Z80 エミュレータの初期化
		z80_set_reg(Z80_REG_SP, 0xfffe);
		z80_set_reg(Z80_REG_PC, 0x0050);
	}
	z80_emulate(10000);

	Unlock();

	return true;
}

//
// 停止
//
bool SccDriver::Stop()
{
	Lock();

	// ＳＳＧとＳＣＣ停止、すげームリヤリかも :-)
	SccDriver::sWritePort(0xa0, 0x07);
	SccDriver::sWritePort(0xa1, 0x3F);
	for (int i = 0x08; i < 0x10; i++) {
		SccDriver::sWritePort(0xa0, i);
		SccDriver::sWritePort(0xa1, 0x00);
	}
	if (m_051649) {
		m_051649->InitSccWork();
	}
	z80_reset();

	// RAM 領域初期化
	memset(&ram[0xc000], 0, 0x4000);

	// ROM の場合
	if (sd_snatcher == 0) {

		// SCC の場合、マッピング状態初期化
		if (m_type == TYPE_SCC) {
			z80_map_fetch(mapadr, 0xbfff, &rom[mapofs]);
			z80_map_read(mapadr, 0xbfff, &rom[mapofs]);
		}

		// Z80 エミュレータの初期化
		z80_set_reg(Z80_REG_PC, 0x0000);
		z80_set_reg(Z80_REG_SP, 0xfffe);
	// SD SNATCHER の場合
	} else {
		// Z80 エミュレータの初期化
		z80_set_reg(Z80_REG_PC, 0x0050);
		z80_set_reg(Z80_REG_SP, 0xfffe);
	}		

	z80_emulate(50000);

	Unlock();

	return true;
}

// ドライバ情報
static ssDriverDescription Description[] =
{
	{
		"msx",
		"scc",
		{"KONAMI MSX (SCC)"},
		{
			// Files
			{"code",	{"program code"}},
		},
		{
			// Options
			{"psg_mix",		0xa5,		{"AY-3-8910 mixing level"}},
			{"scc_mix",		0x84,		{"051649 mixing level"}},
			{"mapadr",		0x6000,		{"map adderss"}},
			{"mapofs",		0x10000,	{"map offset"}},
			{"init_adr",	0x0000,		{"init procedure adderss", "if 0, disable"}},
			{"cmd_adr",		0x6000,		{"sound code procedure adderss"}},
			{"irq_adr",		0x6000,		{"interrupt procedure adderss"}},
		},
	},
	{
		"msx",
		"konami_psg",
		{"KONAMI MSX (PSG)"},
		{
			// Files
			{"code",	{"program code"}},
		},
		{
			// Options
			{"psg_mix",		0xa5,		{"AY-3-8910 mixing level"}},
			{"mapadr",		0x6000,		{"map adderss"}},
			{"mapofs",		0x10000,	{"map offset"}},
			{"init_adr",	0x0000,		{"init procedure adderss", "if 0, disable"}},
			{"cmd_adr",		0x6000,		{"sound code procedure adderss"}},
			{"irq_adr",		0x6000,		{"interrupt procedure adderss"}},
		},
	},
	0
};

//
// ドライバの登録
//
static ssSoundDriver *CreateDriver(const ssDriverConfig *_config)
{
	if (_config->driver_majortype == "msx") {
		if (_config->driver_subtype == "scc") {
			return SccDriver::Instance(SccDriver::TYPE_SCC);
		}
		else if (_config->driver_subtype == "konami_psg") {
			return SccDriver::Instance(SccDriver::TYPE_PSG);
		}
	}
	return NULL;
}

static ssDriverRegister msx(string("msx"), CreateDriver, Description);
