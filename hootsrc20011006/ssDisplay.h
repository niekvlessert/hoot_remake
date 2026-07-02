#ifndef __SSDISPLAY_H__
#define __SSDISPLAY_H__

#include <ddraw.h>

#include "ssSoundDriverManager.h"
#include "ssTrackInfo.h"
#include "ssIfFft.h"
#include "ssIfFolder.h"

class ssDisplay
{
private:
	enum {
		MAX_TRACK = 16,
		MAX_INT_TRACK = 80,
		MAX_SELECT = 6,
		MAX_INFO = 64,
	};
	enum {
		C_TRK_HEIGHT = 22,

		C_LVL_POS_X = 22 + 6 * 20,
		C_LVL_POS_Y = 17,
		C_LVL_WIDTH = 64,
		C_LVL_HEIGHT = 4,

		C_PAN_POS_X = C_LVL_POS_X + C_LVL_WIDTH + 8,
		C_PAN_POS_Y = C_LVL_POS_Y,
		C_PAN_WIDTH = 32,
		C_PAN_HEIGHT = 4,

		C_INF_POS_X = 22,
		C_INF_POS_Y = 14,
		C_INF_WIDTH = 92,
		C_INF_HEIGHT = 8,

		C_CURSOR_POS_X = C_INF_POS_X - 8,
		C_CURSOR_POS_Y = C_INF_POS_Y,
		C_CURSOR_WIDTH = 6,
		C_CURSOR_HEIGHT = 8,

		C_KBD_POS_X = 18,
		C_KBD_POS_Y = 22,
		C_KBD_WIDTH = 35,
		C_KBD_HEIGHT = 12,

		C_CH_POS_X = 2,
		C_CH_POS_Y = 18,

		C_SEL_POS_X = 0,
		C_SEL_POS_Y = 384,
		C_SEL_WIDTH = 640,
		C_SEL_HEIGHT = 96,

		C_REG_POS_X = 428,
		C_REG_POS_Y = 176,

		C_SPE_POS_X = 428,
		C_SPE_POS_Y = 16,
		C_SPE_WIDTH = 12,
		C_SPE_HEIGHT = 64,
		C_SPE_STEP  = 80,

	};
	enum {
		KBD_BASE_X = 0,
		KBD_BASE_Y = 0,

		PAN_BASE_X = 48,
		PAN_BASE_Y = 0,

		FONT_BASE_X = 0,
		FONT_BASE_Y = 160,

		LCD_BASE_X = 0,
		LCD_BASE_Y = 240,
		CH_BASE_X = 0,
		CH_BASE_Y = LCD_BASE_Y - 8,
		MASK_CUR_BASE_X = 12,
		MASK_CUR_BASE_Y = LCD_BASE_Y - 8,

		SPE_BASE_X = 0,
		SPE_BASE_Y = 260,
	};
	enum {
		REDRAW_KEYBOARD = 0x00000001,
		REDRAW_LEVEL    = 0x00000002,
		REDRAW_PAN      = 0x00000004,
		REDRAW_SPECTRUM = 0x00000008,
		REDRAW_SELECTER = 0x00000010,
		REDRAW_REGISTER = 0x00000020,
		REDRAW_CURSOR   = 0x00000040,
		REDRAW_ALL      = 0xffffffff,
	};
	enum {
		COL_SEL_FOREGROUND = RGB(113, 120, 255),
		COL_SEL_BACKGROUND = RGB(32, 32, 50),

		COL_LVL_FOREGROUND = RGB(192, 0, 0),
		COL_LVL_BACKGROUND = RGB(64, 0, 0),

		COL_INF_BACKGROUND = RGB(0, 0, 128),
	};

public:
	ssDisplay();
	~ssDisplay();

	bool Initialize(HWND _hWnd, ssSoundDriverManager *_psdm);
	bool Destroy(void);

	bool InitializeSurfaces(void);
	bool SwitchFullScreen(void);

	void Update(void);

	void Redraw(void);
	void Flip(void);

	void OnKeyDown(UINT _key);
	void OnKeyUp(UINT _key);

	void OnMove(int _x, int _y);

	void DropFile(const string &_fname);

protected:
	void RecognizePixelFormat(LPDIRECTDRAWSURFACE7 _lpdds);

	void RedrawRequest(DWORD _flag = 0);
	void ClearInfo(void);

	void FolderSelect(UINT _key);
	void FolderSelectUpOne(int _size);
	void FolderSelectDownOne(int _size);
	void TitleSelect(UINT _key);
	void MaskSelect(UINT _key);

	void DrawKeyboard(int _trk, int _oct, int _note);
	void DrawPan(int _trk, BYTE _pan);
	void DrawSpectrum(int _ch, int _freq, BYTE _level);

	void Putchar(int _x, int _y, char _ch);
	void PutcharNoColorKey(int _x, int _y, char _ch);
	void Print(int _x, int _y, char *_str);
	void PrintHEX1(int _x, int _y, int _num);
	void PrintHEX2(int _x, int _y, int _num);
	void PrintHEX3(int _x, int _y, int _num);
	void PrintHEX4(int _x, int _y, int _num);

	void PutcharLCD(int _x, int _y, char _ch);
	void PrintLCD(int _x, int _y, char *_str);

	void DrawFolderList(HDC _hDC);
	void DrawTitleList(HDC _hDC);

private:
	HWND m_hWnd;
	ssSoundDriverManager *m_psdm;
	ssIfFft *m_pfft;
	int m_FftNumSpectrum;
	bool m_bFullScreen;
	bool m_bScreenChangeReady;
	RECT m_rcWindow;
	LONG m_WindowStyle;
	LONG m_WindowStyleEx;
	HMENU m_hMenu;

	// for DirectDraw
	LPDIRECTDRAW7 lpDirectDraw;
	LPDIRECTDRAWSURFACE7 lpDDSPrimary;
	LPDIRECTDRAWSURFACE7 lpDDSBack;
	LPDIRECTDRAWSURFACE7 lpDDSData;
	enum {
		DMODE_UNKNOWN = 0,
		DMODE_RGB8,
		DMODE_RGB555,
		DMODE_RGB565,
		DMODE_RGB24,
		DMODE_RGB32,
	};
	int m_DisplayMode;
	HFONT m_Font;

	enum {
		KEY_CONTROL	= 0x01,
		KEY_S		= 0x02,
		KEY_F		= 0x04,
		KEY_D		= 0x08,
	};
	enum {
		KEY_STATE_SELECTER = 0,
		KEY_STATE_MASK,
	};

	ssDriverConfig *m_ConfigSingleFile;
	ssIfFolder *m_CurrentFolder;
	int m_HomeNumber;
	int m_SelectedNumber;

	int m_SpeedCtrlKeyFlag;

	int m_KeyState;

	// for Drawing
	int m_HomeTrack;
	DWORD m_RedrawFlag;
	struct {
		BYTE keycode;
		BYTE pan;
		bool mask;
	} dispstat[MAX_TRACK];
	struct {
		int level;
		bool mask;
	} trkstat[MAX_INFO][MAX_INT_TRACK];
	ssTrackInfo trkinfo[MAX_INFO][MAX_INT_TRACK];
	ssFftLevel *fftinfo[2][MAX_INFO];
	ssFftLevel *fftdispinfo[2];
	int m_CurrentInfo;
	int m_info_buffer;
	int m_CursorPos;

	enum {
		REG_SIZE = 0x300,
		REG_DISP_SIZE = 384,
	};
	int m_ChipIndex;
	int m_RegisterOffset;
	BYTE regstat[MAX_INFO][REG_SIZE];
	BYTE regdispstat[REG_DISP_SIZE];

	// for Color
	DWORD m_ColSELFG;
	DWORD m_ColSELBG;

	DWORD m_ColLVLFG;
	DWORD m_ColLVLBG;

	DWORD m_ColINFBG;
};

#endif // __SSDISPLAY_H__
