#include "StdAfx.h"
#include "ssSoundDriverManager.h"
#include "ssTrackInfo.h"
#include "ssSoundChip.h"
#include <math.h>

#include <ddraw.h>
#include "ms/ddutil.h"

#include "resource.h"

#include "ssDisplay.h"


//#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ddraw.lib")

// •\Ž¦AUIŽü‚è


#define SAFERELEASE(p) {if (p) {(p)->Release(); p = NULL;}}

static inline DWORD RGB565(BYTE r, BYTE g, BYTE b) {
	return ((r>>3) << 11) + ((g>>2) << 5) + (b>>3);
}

static inline DWORD RGB_RGB565(DWORD _rgb) {
	const BYTE r = _rgb;
	const BYTE g = _rgb >> 8;
	const BYTE b = _rgb >> 16;
	return RGB565(r, g, b);
}

static inline DWORD RGB555(BYTE r, BYTE g, BYTE b) {
	return ((r>>3) << 10) + ((g>>3) << 5) + (b>>3);
}

static inline DWORD RGB_RGB555(DWORD _rgb) {
	const BYTE r = _rgb;
	const BYTE g = _rgb >> 8;
	const BYTE b = _rgb >> 16;
	return RGB555(r, g, b);
}

static inline DWORD RGB24(BYTE r, BYTE g, BYTE b) {
	return (r << 16) + (g << 8) + (b);
}

static inline DWORD RGB_RGB24(DWORD _rgb) {
	const BYTE r = _rgb;
	const BYTE g = _rgb >> 8;
	const BYTE b = _rgb >> 16;
	return RGB24(r, g, b);
}

ssDisplay::ssDisplay()
{
	m_psdm = NULL;
	m_pfft = NULL;
	m_FftNumSpectrum = 0;

	m_hWnd = NULL;
	m_bFullScreen = false;
	m_bScreenChangeReady = true;

	m_ConfigSingleFile = new ssDriverConfig(true);
	m_CurrentFolder = NULL;
	m_HomeNumber = 0;
	m_SelectedNumber = 0;

	m_CurrentInfo = MAX_INFO - 1;

	m_ChipIndex = 0;
	m_RegisterOffset = 0;

	m_SpeedCtrlKeyFlag = 0;
	m_KeyState = KEY_STATE_SELECTER;
	m_CursorPos = 0;

	m_HomeTrack = 0;
	m_RedrawFlag = REDRAW_ALL;

	{
		for (int i = 0; i < MAX_INFO; i++) {
			fftinfo[0][i] = NULL;
			fftinfo[1][i] = NULL;

			memset(regstat[i], 0, REG_SIZE);
		}
		fftdispinfo[0] = NULL;
		fftdispinfo[1] = NULL;

		memset(regdispstat, 0xff, REG_DISP_SIZE);
	}
}

ssDisplay::~ssDisplay()
{
	Destroy();

	{
		for (int ch = 0; ch < 2; ch++) {
			for (int i = 0; i < MAX_INFO; i++) {
				if (fftinfo[ch][i]) {
					delete[] fftinfo[ch][i];
					fftinfo[ch][i] = NULL;
				}
			}
			if (fftdispinfo[ch]) {
				delete[] fftdispinfo[ch];
				fftdispinfo[ch] = NULL;
			}
		}
	}

	delete m_ConfigSingleFile;
}

bool ssDisplay::Initialize(HWND _hWnd, ssSoundDriverManager *_psdm)
{
	HRESULT hRet;

	m_hWnd = _hWnd;
	m_psdm = _psdm;
	ASSERT(m_psdm != NULL);
	m_CurrentFolder = m_psdm->GetRootFolder();
	ASSERT(m_CurrentFolder != NULL);

	m_pfft = m_psdm->GetFft();
	ASSERT(m_pfft != NULL);
	m_FftNumSpectrum = m_pfft->GetNumSpectrum();

	const ssConfig &config = m_psdm->GetConfig();
	m_info_buffer = config.info_buffer;

	{
		for (int ch = 0; ch < 2; ch++) {
			for (int i = 0; i < MAX_INFO; i++) {
				fftinfo[ch][i] = new ssFftLevel[m_FftNumSpectrum];
			}
			fftdispinfo[ch] = new ssFftLevel[m_FftNumSpectrum];
		}
	}
	ClearInfo();

	::GetWindowRect(m_hWnd, &m_rcWindow);

	hRet = DirectDrawCreateEx(NULL, (VOID**)&lpDirectDraw, IID_IDirectDraw7, NULL);

	InitializeSurfaces();

	LOGFONT lf;
	lf.lfHeight = 16;
	lf.lfWidth = 0;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = FW_NORMAL;
	lf.lfItalic = FALSE;
	lf.lfUnderline = FALSE;
	lf.lfStrikeOut = FALSE;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	CString face;
	face.LoadString(IDS_SELECTOR_FONT);
	strcpy(lf.lfFaceName, (LPCSTR)face);
	m_Font = CreateFontIndirect(&lf);

	return true;
}

bool ssDisplay::Destroy(void)
{
	DeleteObject(m_Font);
	if (lpDirectDraw) {
		lpDirectDraw->SetCooperativeLevel(m_hWnd, DDSCL_NORMAL);
	}
	SAFERELEASE(lpDDSPrimary);
	SAFERELEASE(lpDDSBack);
	SAFERELEASE(lpDDSData);
	SAFERELEASE(lpDirectDraw);

	return true;
}

bool ssDisplay::InitializeSurfaces(void)
{
	DDSURFACEDESC2 ddsd;
	LPDIRECTDRAWCLIPPER pClipper;
	HRESULT hRet;

	if (m_bFullScreen) {
		hRet = lpDirectDraw->SetCooperativeLevel(m_hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
		if (hRet != DD_OK) {
			OutputDebugString("FAIL: SetCooperativeLevel\n");
			return false;
		}

		hRet = lpDirectDraw->SetDisplayMode(640, 480, 16, 0, 0);
		if (hRet != DD_OK) {
			OutputDebugString("FAIL: SetDisplayMode\n");
			return false;
		}

		ZeroMemory(&ddsd,sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		hRet = lpDirectDraw->CreateSurface(&ddsd, &lpDDSPrimary, NULL);

		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
		ddsd.dwWidth = 640;
		ddsd.dwHeight = 480;
		hRet = lpDirectDraw->CreateSurface(&ddsd, &lpDDSBack, NULL);
		if (hRet != DD_OK) {
			OutputDebugString("FAIL: Offscreen\n");
			return false;
		}

		RecognizePixelFormat(lpDDSBack);

		lpDDSData = ::DDLoadBitmap(lpDirectDraw, "hoot", 0, 0);

		DDCOLORKEY ddck;
		ddck.dwColorSpaceLowValue = 0;
		ddck.dwColorSpaceHighValue = 0;
		lpDDSData->SetColorKey(DDCKEY_SRCBLT, &ddck);

	} else {
		lpDirectDraw->SetCooperativeLevel(m_hWnd, DDSCL_NORMAL);

		ZeroMemory(&ddsd,sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		hRet = lpDirectDraw->CreateSurface(&ddsd, &lpDDSPrimary, NULL);

		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
		ddsd.dwWidth = 640;
		ddsd.dwHeight = 480;
		hRet = lpDirectDraw->CreateSurface(&ddsd, &lpDDSBack, NULL);
		if (hRet != DD_OK) {
			OutputDebugString("FAIL: Offscreen\n");
			return false;
		}

		RecognizePixelFormat(lpDDSBack);

		lpDDSData = ::DDLoadBitmap(lpDirectDraw, "hoot", 0, 0);

		DDCOLORKEY ddck;
		ddck.dwColorSpaceLowValue = 0;
		ddck.dwColorSpaceHighValue = 0;
		lpDDSData->SetColorKey(DDCKEY_SRCBLT, &ddck);

		hRet = lpDirectDraw->CreateClipper(0, &pClipper, NULL);

		pClipper->SetHWnd(0, m_hWnd);
		lpDDSPrimary->SetClipper(pClipper);
		pClipper->Release();
	}

	return true;
}

bool ssDisplay::SwitchFullScreen(void)
{
	if (!m_bScreenChangeReady) {
		return false;
	}

	m_bScreenChangeReady = false;

	m_bFullScreen = !m_bFullScreen;

	lpDirectDraw->SetCooperativeLevel(m_hWnd, DDSCL_NORMAL);
	SAFERELEASE(lpDDSPrimary);
	SAFERELEASE(lpDDSBack);
	SAFERELEASE(lpDDSData);

	if (m_bFullScreen) {
		::GetWindowRect(m_hWnd, &m_rcWindow);
		m_WindowStyle = ::GetWindowLong(m_hWnd, GWL_STYLE);
		m_WindowStyleEx = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
		//m_WindowMenu = ::GetClassLong(m_hWnd, GCL_MENUNAME);

		::SetWindowLong(m_hWnd, GWL_STYLE, WS_POPUP);
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);
		m_hMenu = ::GetMenu(m_hWnd);
		::SetMenu(m_hWnd, NULL);
	}

	if (!m_bFullScreen) {
		::SetWindowLong(m_hWnd, GWL_STYLE, m_WindowStyle);
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, m_WindowStyleEx);
		::SetMenu(m_hWnd, m_hMenu);
		int width = m_rcWindow.right - m_rcWindow.left;
		int height = m_rcWindow.bottom - m_rcWindow.top;
		BOOL b = ::SetWindowPos(m_hWnd,
			HWND_NOTOPMOST,
			m_rcWindow.left, m_rcWindow.top,
			width,
			height,
			SWP_SHOWWINDOW);
#if 0
		char s[64];
		sprintf(s, "T(%d - %d) - (%d - %d) -> %d\n",
			m_rcWindow.left,
			m_rcWindow.top,
			m_rcWindow.right,
			m_rcWindow.bottom,
			b);
		OutputDebugString(s);
#endif
	}

	InitializeSurfaces();

	RedrawRequest(REDRAW_ALL);

	m_bScreenChangeReady = true;

	return true;
}

void ssDisplay::RecognizePixelFormat(LPDIRECTDRAWSURFACE7 _lpdds)
{
	DDPIXELFORMAT pf;
	memset(&pf, 0, sizeof(pf));
	pf.dwSize = sizeof(pf);
	_lpdds->GetPixelFormat(&pf);
	if (pf.dwRGBBitCount == 16) {
		if (pf.dwRBitMask == 0xf800 &&
			pf.dwGBitMask == 0x07e0 &&
			pf.dwBBitMask == 0x001f) {

			m_DisplayMode = DMODE_RGB565;
			m_ColSELFG = RGB_RGB565(COL_SEL_FOREGROUND);
			m_ColSELBG = RGB_RGB565(COL_SEL_BACKGROUND);
			m_ColLVLFG = RGB_RGB565(COL_LVL_FOREGROUND);
			m_ColLVLBG = RGB_RGB565(COL_LVL_BACKGROUND);
			m_ColINFBG = RGB_RGB565(COL_INF_BACKGROUND);
		} else if (pf.dwRBitMask == 0x7c00 &&
			pf.dwGBitMask == 0x03e0 &&
			pf.dwBBitMask == 0x001f) {

			m_DisplayMode = DMODE_RGB555;
			m_ColSELFG = RGB_RGB555(COL_SEL_FOREGROUND);
			m_ColSELBG = RGB_RGB555(COL_SEL_BACKGROUND);
			m_ColLVLFG = RGB_RGB555(COL_LVL_FOREGROUND);
			m_ColLVLBG = RGB_RGB555(COL_LVL_BACKGROUND);
			m_ColINFBG = RGB_RGB555(COL_INF_BACKGROUND);
		}
	} else if (pf.dwRGBBitCount == 24) {
		m_DisplayMode = DMODE_RGB24;
		m_ColSELFG = RGB_RGB24(COL_SEL_FOREGROUND);
		m_ColSELBG = RGB_RGB24(COL_SEL_BACKGROUND);
		m_ColLVLFG = RGB_RGB24(COL_LVL_FOREGROUND);
		m_ColLVLBG = RGB_RGB24(COL_LVL_BACKGROUND);
		m_ColINFBG = RGB_RGB24(COL_INF_BACKGROUND);
	} else if (pf.dwRGBBitCount == 32) {
		m_DisplayMode = DMODE_RGB32;
		m_ColSELFG = RGB_RGB24(COL_SEL_FOREGROUND);
		m_ColSELBG = RGB_RGB24(COL_SEL_BACKGROUND);
		m_ColLVLFG = RGB_RGB24(COL_LVL_FOREGROUND);
		m_ColLVLBG = RGB_RGB24(COL_LVL_BACKGROUND);
		m_ColINFBG = RGB_RGB24(COL_INF_BACKGROUND);
	} else {
		m_DisplayMode = DMODE_UNKNOWN;
		m_ColSELFG = COL_SEL_FOREGROUND;
		m_ColSELBG = COL_SEL_BACKGROUND;
		m_ColLVLFG = COL_LVL_FOREGROUND;
		m_ColLVLBG = COL_LVL_BACKGROUND;
		m_ColINFBG = COL_INF_BACKGROUND;
	}
}

void ssDisplay::Update(void)
{
	ssTrackInfo *info;
	const int track_count = m_psdm->GetTrackCount();

	m_CurrentInfo++;
	m_CurrentInfo %= MAX_INFO;

	const int ci = m_CurrentInfo;
	const int pi = (m_CurrentInfo - 1 + MAX_INFO) % MAX_INFO;

	for (int trk = 0; trk < track_count; trk++) {
		if (trk >= MAX_INT_TRACK) {
			break;
		}

		info = m_psdm->GetInfo(trk);
		memcpy(&trkinfo[ci][trk], info, sizeof(ssTrackInfo));
		trkstat[ci][trk].level = trkstat[pi][trk].level;
		trkstat[ci][trk].mask = m_psdm->GetMask(trk);
		if (trkstat[ci][trk].mask) {
			trkstat[ci][trk].level = 0;
			trkinfo[ci][trk].pan = 8; // OFF
		} else {
			if (info->keyon & 0x80) {
				info->keyon = 0x7f;

				trkstat[ci][trk].level = info->volume / 2;
			} else if (trkstat[ci][trk].level > 0) {
				if (info->keyon) {
					trkstat[ci][trk].level -= 1;
					m_RedrawFlag |= REDRAW_LEVEL;
				} else {
					trkstat[ci][trk].level -= 2;
					if (trkstat[ci][trk].level < 0) {
						trkstat[ci][trk].level = 0;
					}
					m_RedrawFlag |= REDRAW_LEVEL;
				}
			}
		}
	}

	ssSoundChip *chip = m_psdm->GetSoundChip(m_ChipIndex);
	if (chip != NULL) {
		int ct;
		ct = chip->GetRegs(regstat[ci], REG_SIZE, 0);
		if (ct != REG_SIZE) {
			memset(regstat[ci] + ct, 0, REG_SIZE - ct);
		}
	} else {
		memset(regstat[ci], 0, REG_SIZE);
	}

	for ( ; trk < MAX_TRACK; trk++) {
		if (trkstat[ci][m_HomeTrack + trk].level != 0) {
			trkstat[ci][m_HomeTrack + trk].level = 0;
			m_RedrawFlag |= REDRAW_LEVEL;
		}
	}
	{
		int size = m_FftNumSpectrum * sizeof(ssFftLevel);
		ssFftLevel *sp;
		sp = m_pfft->GetSpectrum(ssIfFft::CH_L);
		memcpy(fftinfo[0][ci], sp, size);
		sp = m_pfft->GetSpectrum(ssIfFft::CH_R);
		memcpy(fftinfo[1][ci], sp, size);
	}

}

void ssDisplay::RedrawRequest(DWORD _flag)
{
	m_RedrawFlag |= _flag;
	InvalidateRect(m_hWnd, NULL, FALSE);
}

void ssDisplay::ClearInfo(void)
{
	m_ChipIndex = 0;
	m_RegisterOffset = 0;

	for (int i = 0; i < MAX_INFO; i++) {
		int trk;
		for (trk = 0; trk < MAX_INT_TRACK; trk++) {
			trkstat[i][trk].level = 0;
			trkstat[i][trk].mask = false;
			InitTrackInfo(&trkinfo[i][trk]);
			trkinfo[i][trk].pan = 0;
		}
		for (trk = 0; trk < MAX_TRACK; trk++) {
			dispstat[trk].keycode = 0xff;
			dispstat[trk].pan = 0xff;
			dispstat[trk].mask = false;
		}
		for (int ch = 0; ch < 2; ch++) {
			for (int x = 0; x < m_FftNumSpectrum; x++) {
				fftinfo[ch][i][x] = 0;
				fftdispinfo[ch][x] = 0xff;
			}
		}
		memset(regstat[i], 0, REG_SIZE);
	}

	memset(regdispstat, 0xff, REG_DISP_SIZE);
}


inline void ssDisplay::Putchar(int _x, int _y, char _ch)
{
	RECT srcrc;
	const int fx = _ch % 16;
	const int fy = _ch / 16;

	srcrc.left = FONT_BASE_X + fx * 6;
	srcrc.top = FONT_BASE_Y + fy * 8;
	srcrc.right = srcrc.left + 6;
	srcrc.bottom = srcrc.top + 8;

	lpDDSBack->BltFast(_x, _y, lpDDSData, &srcrc, DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);
}


inline void ssDisplay::PutcharNoColorKey(int _x, int _y, char _ch)
{
	RECT srcrc;
	const int fx = _ch % 16;
	const int fy = _ch / 16;

	srcrc.left = FONT_BASE_X + fx * 6;
	srcrc.top = FONT_BASE_Y + fy * 8;
	srcrc.right = srcrc.left + 6;
	srcrc.bottom = srcrc.top + 8;

	lpDDSBack->BltFast(_x, _y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
}


void ssDisplay::Print(int _x, int _y, char *_str)
{
	char ch;
	while ((ch = *_str++) != '\0') {
		Putchar(_x, _y, ch);
		_x += 6;
	}
}


static const char hexchar[] = "0123456789ABCDEF";

void ssDisplay::PrintHEX1(int _x, int _y, int _num)
{
	PutcharNoColorKey(_x, _y, hexchar[(_num    ) & 15]);
}


void ssDisplay::PrintHEX2(int _x, int _y, int _num)
{
	PutcharNoColorKey(_x, _y, hexchar[(_num>> 4) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num    ) & 15]);
}


void ssDisplay::PrintHEX3(int _x, int _y, int _num)
{
	PutcharNoColorKey(_x, _y, hexchar[(_num>> 8) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num>> 4) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num    ) & 15]);
}


void ssDisplay::PrintHEX4(int _x, int _y, int _num)
{
	PutcharNoColorKey(_x, _y, hexchar[(_num>>12) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num>> 8) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num>> 4) & 15]);
	_x += 6;
	PutcharNoColorKey(_x, _y, hexchar[(_num    ) & 15]);
}


inline void ssDisplay::PutcharLCD(int _x, int _y, char _ch)
{
	RECT srcrc;
	const int fx = _ch * 6;

	srcrc.left = LCD_BASE_X + fx;
	srcrc.top = LCD_BASE_Y;
	srcrc.right = srcrc.left + 6;
	srcrc.bottom = srcrc.top + 8;

	lpDDSBack->BltFast(_x, _y, lpDDSData, &srcrc, DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);
	//lpDDSBack->BltFast(_x, _y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
}

void ssDisplay::PrintLCD(int _x, int _y, char *_str)
{
	char ch;
	while ((ch = *_str++) != '\0') {
		PutcharLCD(_x, _y, ch - '0');
		_x += 6;
	}
}

void ssDisplay::DrawKeyboard(int _trk, int _oct, int _note)
{
	RECT srcrc;
	DWORD x, y;

	srcrc.left = KBD_BASE_X;
	srcrc.top = KBD_BASE_Y + C_KBD_HEIGHT * (_note+1);
	srcrc.right = C_KBD_WIDTH;
	srcrc.bottom = srcrc.top + C_KBD_HEIGHT;

	x = C_KBD_POS_X + _oct * C_KBD_WIDTH;
	y = C_KBD_POS_Y + _trk * C_TRK_HEIGHT;

	lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
}

void ssDisplay::DrawPan(int _trk, BYTE _pan)
{
	RECT srcrc;
	DWORD x, y;

	srcrc.left = PAN_BASE_X;
	srcrc.top = PAN_BASE_Y + C_PAN_HEIGHT * _pan;
	srcrc.right = srcrc.left + C_PAN_WIDTH;
	srcrc.bottom = srcrc.top + C_PAN_HEIGHT;

	x = C_PAN_POS_X;
	y = C_PAN_POS_Y + _trk * C_TRK_HEIGHT;

	lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
}

void ssDisplay::DrawSpectrum(int _ch, int _freq, BYTE _level)
{
	RECT srcrc;
	DWORD x, y;

	srcrc.left = SPE_BASE_X + C_SPE_WIDTH * _level;
	srcrc.top = SPE_BASE_Y;
	srcrc.right = srcrc.left + C_SPE_WIDTH;
	srcrc.bottom = srcrc.top + C_SPE_HEIGHT;

	x = C_SPE_POS_X + _freq * C_SPE_WIDTH;
	y = C_SPE_POS_Y + _ch * C_SPE_STEP;

	lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
}

void ssDisplay::Redraw(void)
{
	RECT srcrc;
	RECT dstrc;

	DDBLTFX fx;
	memset(&fx, 0, sizeof(fx));

	const int di = (m_CurrentInfo - m_info_buffer + MAX_INFO) % MAX_INFO;

	if (lpDDSData->IsLost() != DD_OK) {
		lpDDSData->Restore();
		DDReLoadBitmap(lpDDSData, "hoot");
		m_RedrawFlag = REDRAW_ALL;
	}

	if (lpDDSBack->IsLost() != DD_OK) {
		lpDDSBack->Restore();
		m_RedrawFlag = REDRAW_ALL;
	}

	if (m_RedrawFlag == REDRAW_ALL) {
		dstrc.top = 0;
		dstrc.bottom = 480;
		dstrc.left = 0;
		dstrc.right = 640;

		fx.dwSize = sizeof(fx);
		fx.dwFillColor = 0;
		lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
	}

	if (m_RedrawFlag & REDRAW_KEYBOARD) {
		m_RedrawFlag &= ~REDRAW_KEYBOARD;
		// ”wŒi‚Ì“]‘—
#if 0
		srcrc.left = 0;
		srcrc.top = 0;
		srcrc.right = 640;
		srcrc.bottom = 480;
		lpDDSBack->BltFast(0, 0, lpDDSData, &srcrc, DDBLTFAST_WAIT);
#else
		dstrc.top = 0;
		dstrc.bottom = 384;
		dstrc.left = 0;
		dstrc.right = 428;

		fx.dwSize = sizeof(fx);
		fx.dwFillColor = 0;
		lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
#endif

		Print(C_KBD_POS_X, 0, "hoot... - Sound Hardware Emulator Copyright \1771999-2001 DMP SOFT.");

		const int track_count = m_psdm->GetTrackCount();

		// Œ®”ÕŒÅ’è•”•ª‚Ì•`‰æ
		for (int trk = 0; trk < MAX_TRACK; trk++) {
			const int _trk = m_HomeTrack + trk;
			{
				int x = C_CH_POS_X;
				int y = C_CH_POS_Y + trk * C_TRK_HEIGHT;
				srcrc.left = CH_BASE_X;
				srcrc.top = CH_BASE_Y;
				srcrc.right = CH_BASE_X + 12;
				srcrc.bottom = CH_BASE_Y + 8;
				lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);
				//lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT);
				char s[16];
				wsprintf(s, "%02d", _trk);
				PrintLCD(x, y + 8, s);
			}

			int moct = -1;
			if (trkstat[di][m_HomeTrack + trk].mask || trk >= track_count) {
				moct = 12;
			}
			for (int oct = 0; oct < 11; oct++) {
				DrawKeyboard(trk, oct, moct);
			}
			dispstat[trk].mask = trkstat[di][m_HomeTrack + trk].mask;
		}

		// ‰¹Œ¹î•ñ‚Ì•`‰æ
		fx.dwSize = sizeof(fx);
		for (trk = 0; trk < MAX_TRACK; trk++) {
			const int _trk = m_HomeTrack + trk;
			dstrc.top = C_INF_POS_Y + trk * C_TRK_HEIGHT;
			dstrc.bottom = dstrc.top + C_INF_HEIGHT;
			dstrc.left = C_INF_POS_X;
			dstrc.right = dstrc.left + C_INF_WIDTH;

			fx.dwFillColor = m_ColINFBG;
			//lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);

			if (trk < track_count) {
				ssTrackInfo *info = m_psdm->GetInfo(_trk);
				int x = C_INF_POS_X;
				int y = C_INF_POS_Y + trk * C_TRK_HEIGHT;
				Print(x, y, info->name);
			}
		}
	}

	{
		// Œ®”Õ‚Ì•`‰æ
		const int track_count = m_psdm->GetTrackCount();
		for (int trk = 0; trk < track_count; trk++) {
			const int _trk = m_HomeTrack + trk;

			if (trk >= MAX_TRACK) {
				break;
			}

			// mask
			const bool mask = trkstat[di][_trk].mask;
			if (dispstat[trk].mask != mask) {
				int moct = -1;
				if (trkstat[di][m_HomeTrack + trk].mask) {
					moct = 12;
				}
				for (int oct = 0; oct < 11; oct++) {
					DrawKeyboard(trk, oct, moct);
				}
				dispstat[trk].mask = mask;
				dispstat[trk].keycode = 0xff;
			}

			if (!mask){
				// Œ®”Õ
				if (trkinfo[di][_trk].keyon != 0) {
					const BYTE kc = trkinfo[di][_trk].keycode;
					if (kc != dispstat[trk].keycode) {
						int oct = kc / 12;
						const int note = kc % 12;

						if (oct > 10) oct = 10;

						int poct = dispstat[trk].keycode / 12;
						if (poct > 10) poct = 10;

						if (oct != poct) {
							DrawKeyboard(trk, poct, -1);
						}

						DrawKeyboard(trk, oct, note);

						dispstat[trk].keycode = kc;
					}
				} else {
					if (dispstat[trk].keycode != 0xff) {
						int poct = dispstat[trk].keycode / 12;
						if (poct > 10) poct = 10;
						DrawKeyboard(trk, poct, -1);

						dispstat[trk].keycode = 0xff;
					}
				}
			}
		}
	}

	if (m_RedrawFlag & REDRAW_LEVEL) {
		// ƒŒƒxƒ‹ƒ[ƒ^‚Ì•`‰æ
		//const int track_count = m_psdm->GetTrackCount();
		const int track_count = MAX_TRACK;
		fx.dwSize = sizeof(fx);
		for (int trk = 0; trk < track_count; trk++) {
			const int _trk = m_HomeTrack + trk;
			const int level = trkstat[di][_trk].level;
			dstrc.top = C_LVL_POS_Y + trk * C_TRK_HEIGHT;
			dstrc.bottom = dstrc.top + C_LVL_HEIGHT;
			if (level > 0) {
				dstrc.left = C_LVL_POS_X;
				dstrc.right = dstrc.left + level;

				fx.dwFillColor = m_ColLVLFG;
				lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
			}
			if (level != C_LVL_WIDTH) {
				dstrc.left = C_LVL_POS_X + level;
				dstrc.right = C_LVL_POS_X + C_LVL_WIDTH;

				fx.dwFillColor = m_ColLVLBG;
				lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
			}

			// ƒpƒ“
			const BYTE pan = trkinfo[di][_trk].pan;
			if (pan != dispstat[trk].pan || m_RedrawFlag & REDRAW_PAN) {

				DrawPan(trk, pan);

				dispstat[trk].pan = pan;
			}

		}
		//m_RedrawFlag &= ~(REDRAW_KEYBOARD | REDRAW_PAN);
		//m_RedrawFlag &= ~REDRAW_LEVEL;
	}

	{
		// ƒXƒyƒAƒi‚Ì•`‰æ
		if (m_RedrawFlag & REDRAW_SPECTRUM) {
			for (int ch = 0; ch < 2; ch++) {
				ssFftLevel *sp = fftinfo[ch][di];
				for (int freq = 0; freq < m_FftNumSpectrum; freq++) {
					const ssFftLevel level = sp[freq];
					DrawSpectrum(ch, freq, level);
					fftdispinfo[ch][freq] = level;
				}
			}
			m_RedrawFlag &= ~REDRAW_SPECTRUM;
		}
		for (int ch = 0; ch < 2; ch++) {
			ssFftLevel *sp = fftinfo[ch][di];
			for (int freq = 0; freq < m_FftNumSpectrum; freq++) {
				const ssFftLevel level = sp[freq];
				if (level != fftdispinfo[ch][freq]) {
					DrawSpectrum(ch, freq, level);
					fftdispinfo[ch][freq] = level;
				}
			}
		}
	}

	{
		// ƒŒƒWƒXƒ^î•ñ‚Ì•`‰æ
		int idx, ridx;
		idx = 0;
		ridx = m_RegisterOffset;
		if (m_RedrawFlag & REDRAW_REGISTER) {
			{
				int xo = C_REG_POS_X - (6*3+2);
				int yo = C_REG_POS_Y - (8+1);
				Putchar(xo, yo, '[');
				PrintHEX1(xo + 6, yo, m_ChipIndex + 1);
				Putchar(xo+12, yo, ']');
			}
			for (int x = 0; x < 16; x++) {
				int xo = C_REG_POS_X + x*13;
				int yo = C_REG_POS_Y - (8+1);
				Putchar(xo, yo, '+');
				PrintHEX1(xo + 6, yo, x);
			}
			for (int y = 0; y < (REG_DISP_SIZE/16); y++) {
				PrintHEX3(C_REG_POS_X - (6*3+2), C_REG_POS_Y + y*8, m_RegisterOffset + y*16);
				for (int x = 0; x < 16; x++) {
					BYTE reg = regstat[di][ridx];
					PrintHEX2(C_REG_POS_X + x*13, C_REG_POS_Y + y*8, reg);
					regdispstat[idx] = reg;
					idx++;
					ridx++;
				}
			}
			m_RedrawFlag &= ~REDRAW_REGISTER;
		}
		idx = 0;
		ridx = m_RegisterOffset;
		for (int y = 0; y < (REG_DISP_SIZE/16); y++) {
			for (int x = 0; x < 16; x++) {
				BYTE reg = regstat[di][ridx];
				if (regdispstat[idx] != reg) {
					PrintHEX2(C_REG_POS_X + x*13, C_REG_POS_Y + y*8, reg);
					regdispstat[idx] = reg;
				}
				idx++;
				ridx++;
			}
		}
	}

	if (m_RedrawFlag & REDRAW_SELECTER) {
		m_RedrawFlag &= ~REDRAW_SELECTER;
		// ƒZƒŒƒNƒ^‚Ì•`‰æ
		{
			fx.dwSize = sizeof(fx);
			fx.dwFillColor = m_ColSELBG;

			dstrc.top = C_SEL_POS_Y;
			dstrc.bottom = dstrc.top + C_SEL_HEIGHT;
			dstrc.left = C_SEL_POS_X;
			dstrc.right = dstrc.left + C_SEL_WIDTH;
			lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);

			if (m_KeyState == KEY_STATE_SELECTER) {
				fx.dwSize = sizeof(fx);
				fx.dwFillColor = m_ColSELFG;

				const int l = m_SelectedNumber - m_HomeNumber;
				dstrc.top = C_SEL_POS_Y + l * 16 + 14;
				dstrc.bottom = dstrc.top + 2;
				dstrc.left = C_SEL_POS_X;
				dstrc.right = dstrc.left + C_SEL_WIDTH;
				lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
			}
		}

		HRESULT hr;
		HDC hDC;

		hr = lpDDSBack->GetDC(&hDC);
		if (hr == DD_OK) {
			HFONT oldfont = (HFONT)SelectObject(hDC, m_Font);

			::SetBkMode(hDC, TRANSPARENT);
			::SetTextColor(hDC, COL_SEL_FOREGROUND);
			if (m_CurrentFolder->GetType() == ssIfFolder::TYPE_FOLDER) {
				DrawFolderList(hDC);
			} else if (m_CurrentFolder->GetType() == ssIfFolder::TYPE_NODE) {
				DrawTitleList(hDC);
			}

			SelectObject(hDC, oldfont);

			lpDDSBack->ReleaseDC(hDC);
		}
	}

	if (m_RedrawFlag & REDRAW_CURSOR) {
		for (int trk = 0; trk < MAX_TRACK; trk++) {
			int x = C_CURSOR_POS_X;
			int y = C_CURSOR_POS_Y + trk * C_TRK_HEIGHT;
			if (m_KeyState == KEY_STATE_MASK && trk == m_CursorPos) {
				srcrc.left = MASK_CUR_BASE_X;
				srcrc.top = MASK_CUR_BASE_Y;
				srcrc.right = MASK_CUR_BASE_X + C_CURSOR_WIDTH;
				srcrc.bottom = MASK_CUR_BASE_Y + C_CURSOR_HEIGHT;
				lpDDSBack->BltFast(x, y, lpDDSData, &srcrc, DDBLTFAST_WAIT | DDBLTFAST_SRCCOLORKEY);
			} else {
				dstrc.left = x;
				dstrc.right = x + C_CURSOR_WIDTH;
				dstrc.top = y;
				dstrc.bottom = y + C_CURSOR_HEIGHT;
				fx.dwSize = sizeof(fx);
				fx.dwFillColor = 0;
				lpDDSBack->Blt(&dstrc, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
			}
		}
	}
}

void ssDisplay::DrawFolderList(HDC _hDC)
{
	const ssIfFolder *cf = m_CurrentFolder;
	const int h = m_HomeNumber;
	const int c = cf->GetChildCount();
	for (int l = 0; l < MAX_SELECT; l++ ) {
		const int n = h + l;
		if (n >= c) {
			break;
		}
		ssIfFolder *f = cf->GetChild(n);
		ASSERT(f != NULL);
		string name = f->GetName();
		TextOut(_hDC, C_SEL_POS_X, C_SEL_POS_Y + l*16,
			name.c_str(),
			name.length());
	}
}


void ssDisplay::DrawTitleList(HDC _hDC)
{
	ssDriverConfig *cfg = dynamic_cast<ssDriverConfig *>(m_CurrentFolder);
	ASSERT(cfg != NULL);
	const int h = m_HomeNumber;
	const int c = cfg->GetChildCount();
	for (int l = 0; l < MAX_SELECT; l++ ) {
		if (h + l >= cfg->titlelist.size()) {
			break;
		}
		TextOut(_hDC, C_SEL_POS_X, C_SEL_POS_Y + l*16,
			cfg->titlelist[h + l].name.c_str(),
			cfg->titlelist[h + l].name.length());
	}
}

void ssDisplay::Flip(void)
{
	if (lpDDSBack && m_bScreenChangeReady) {
		RECT srcrc;
		srcrc.left = 0;
		srcrc.top = 0;
		srcrc.right = 640;
		srcrc.bottom = 480;

		if (lpDDSPrimary->IsLost() != DD_OK) {
			lpDDSPrimary->Restore();
		}

		if (lpDDSBack->IsLost() != DD_OK) {
			lpDDSBack->Restore();
			m_RedrawFlag = REDRAW_ALL;
			Redraw();
		}

		if (m_bFullScreen) {
			POINT pt;
			pt.x = 0;
			pt.y = 0;
			//::ClientToScreen(m_hWnd, &pt);
			lpDDSPrimary->BltFast(pt.x, pt.y, lpDDSBack, NULL, DDBLTFAST_WAIT);
		} else {
			RECT dstrc;
			::GetClientRect(m_hWnd, &dstrc);
			::ClientToScreen(m_hWnd, (LPPOINT)&dstrc);
			::ClientToScreen(m_hWnd, (LPPOINT)&dstrc+1);
			lpDDSPrimary->Blt(&dstrc, lpDDSBack, &srcrc, DDBLT_WAIT, NULL);
		}
	}
}

static const char MaskKeyTable[256] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
//	    !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
//	 0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?
	-1, 0, 1, 2, 3, 4, 5, 6, 7,-1,-1,-1,-1,-1,-1, 0,
//	 @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O
	-1,-1,-1,-1,-1,10,-1,-1,-1,15,-1,-1,-1,-1,-1,-1,
//	 P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _
	-1, 8,11,-1,12,14,-1, 9,-1,13,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,

	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

void ssDisplay::OnKeyDown(UINT _key)
{
#if 0
	char s[32];
	wsprintf(s, "[%04x] pushed\n", _key);
	OutputDebugString(s);
#endif
	switch (_key) {
	case 0xbf: // '/'
		{
			int flag;
			flag = m_pfft->Enable(-1);
			m_pfft->Enable(flag == 0 ? 1 : 0);
		}
		break;
	case VK_CONTROL:
	case 'S':
	case 'F':
	case 'D':
		{
			if (_key == VK_CONTROL) {
				m_SpeedCtrlKeyFlag |= KEY_CONTROL;
			} else if (_key == 'S') {
				m_SpeedCtrlKeyFlag |= KEY_S;
			} else if (_key == 'F') {
				m_SpeedCtrlKeyFlag |= KEY_F;
			} else if (_key == 'D') {
				m_SpeedCtrlKeyFlag |= KEY_D;
			}
			if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_S)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_SLOW);
			} else if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_F)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_FAST);
			} else if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_D)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_SILENT);
			}
		}
		break;
	case VK_UP:
		if (m_SpeedCtrlKeyFlag & KEY_CONTROL) {
			if (m_RegisterOffset > 0) {
				m_RegisterOffset -= 0x10;
				RedrawRequest(REDRAW_REGISTER);
			}
		}
		break;
	case VK_DOWN:
		if (m_SpeedCtrlKeyFlag & KEY_CONTROL) {
			if (m_RegisterOffset < (REG_SIZE - REG_DISP_SIZE)) {
				m_RegisterOffset += 0x10;
				RedrawRequest(REDRAW_REGISTER);
			}
		}
		break;
	case VK_RIGHT:
		{
			const int track_count = m_psdm->GetTrackCount();
			if (m_HomeTrack + MAX_TRACK < track_count) {
				m_HomeTrack++;
				if (m_CursorPos > 0) {
					m_CursorPos--;
				}
				RedrawRequest(REDRAW_ALL);
			}
		}
		break;
	case VK_LEFT:
		{
			if (m_HomeTrack > 0) {
				m_HomeTrack--;
				if (m_CursorPos < MAX_TRACK - 1) {
					m_CursorPos++;
				}
				RedrawRequest(REDRAW_ALL);
			}
		}
		break;
	case VK_F1:
	case VK_F2:
	case VK_F3:
	case VK_F4:
		m_ChipIndex = _key - VK_F1;
		m_RegisterOffset = 0;
		RedrawRequest(REDRAW_REGISTER);
		break;
	case '9':
		{
			const int track_count = m_psdm->GetTrackCount();
			for (int trk = 0; trk < track_count; trk++) {
				m_psdm->SetMask(trk, false);
			}
		}
		break;
	case 'O':
		{
			const int track_count = m_psdm->GetTrackCount();
			for (int trk = 0; trk < track_count; trk++) {
				m_psdm->SetMask(trk, true);
			}
		}
		break;
	case '-':
		{
			const int track_count = m_psdm->GetTrackCount();
			for (int trk = 0; trk < track_count; trk++) {
				m_psdm->SetMask(trk, !m_psdm->GetMask(trk));
			}
		}
		break;
	case 'L':
		if (m_psdm->GetFilter()) {
			m_psdm->EnableFilter(false);
		} else {
			m_psdm->EnableFilter(true);
		}
		break;
	}

	if (MaskKeyTable[_key & 0xff] != -1) {
		const int track_count = m_psdm->GetTrackCount();
		const int trk = m_HomeTrack + MaskKeyTable[_key & 0xff];
		if (trk < track_count) {
			m_psdm->SetMask(trk, !m_psdm->GetMask(trk));
		}
	}

	if (!(m_SpeedCtrlKeyFlag & KEY_CONTROL)) {
		switch (m_KeyState) {
		case KEY_STATE_SELECTER:
			if (_key == VK_TAB) {
				const int track_count = m_psdm->GetTrackCount();
				if (track_count != 0) {
					m_KeyState = KEY_STATE_MASK;
					RedrawRequest(REDRAW_SELECTER | REDRAW_CURSOR);
				}
			}
			if (m_CurrentFolder->GetType() == ssIfFolder::TYPE_FOLDER) {
				FolderSelect(_key);
			} else {
				TitleSelect(_key);
			}
			break;
		case KEY_STATE_MASK:
			MaskSelect(_key);
			break;
		}
	}
}

void ssDisplay::OnKeyUp(UINT _key)
{
	switch (_key) {
	case VK_CONTROL:
	case 'S':
	case 'F':
	case 'D':
		{
			if (_key == VK_CONTROL) {
				m_SpeedCtrlKeyFlag &= ~KEY_CONTROL;
			} else if (_key == 'S') {
				m_SpeedCtrlKeyFlag &= ~KEY_S;
			} else if (_key == 'F') {
				m_SpeedCtrlKeyFlag &= ~KEY_F;
			} else if (_key == 'D') {
				m_SpeedCtrlKeyFlag &= ~KEY_D;
			}
			if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_S)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_SLOW);
			} else if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_F)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_FAST);
			} else if (m_SpeedCtrlKeyFlag == (KEY_CONTROL | KEY_D)) {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_SILENT);
			} else {
				m_psdm->SetPlaySpeed(ssSoundDriverManager::SPEED_NORMAL);
			}
		}
		break;
	}
}

void ssDisplay::FolderSelectUpOne(int _size)
{
	if (m_SelectedNumber > 0) {
		m_SelectedNumber--;
	}
	if (m_SelectedNumber < m_HomeNumber) {
		m_HomeNumber--;
	}
}

void ssDisplay::FolderSelectDownOne(int _size)
{
	m_SelectedNumber++;
	if (m_SelectedNumber >= _size) {
		m_SelectedNumber--;
	}
	if (m_SelectedNumber >= m_HomeNumber + MAX_SELECT) {
		m_HomeNumber++;
	}
	if (m_HomeNumber + MAX_SELECT >= _size) {
		m_HomeNumber = _size - MAX_SELECT;
		if (m_HomeNumber < 0) {
			m_HomeNumber = 0;
		}
	}
}

void ssDisplay::FolderSelect(UINT _key)
{
	const int size = m_CurrentFolder->GetChildCount();
	int i;

	switch (_key) {
	case VK_DOWN:
		FolderSelectDownOne(size);
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_UP:
		FolderSelectUpOne(size);
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_NEXT:
		for (i = 0; i < MAX_SELECT; i++) {
			FolderSelectDownOne(size);
		}
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_PRIOR:
		for (i = 0; i < MAX_SELECT; i++) {
			FolderSelectUpOne(size);
		}
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_HOME:
		m_HomeNumber=0;
		m_SelectedNumber = 0;
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_END:
		m_HomeNumber = size - MAX_SELECT;
		if (m_HomeNumber < 0) {
			m_HomeNumber = 0;
		}
		m_SelectedNumber = size - 1;
		if (m_SelectedNumber < 0) {
			m_SelectedNumber = 0;
		}
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_BACK:
	case VK_ESCAPE:
		{
			ssIfFolder *parent = m_CurrentFolder->GetParent();
			if (parent != NULL) {
				ssFolder *pf = dynamic_cast<ssFolder *>(parent);
				if (pf != NULL) {
					m_HomeNumber = pf->GetHome();
					m_SelectedNumber = pf->GetSelected();
					m_CurrentFolder = pf;
					RedrawRequest(REDRAW_SELECTER);
				}
			}
		}
		break;
	case VK_RETURN:
	case VK_SPACE:
		if (m_SelectedNumber < size) {
			ssFolder *cf = dynamic_cast<ssFolder *>(m_CurrentFolder);
			ASSERT(cf != NULL);
			cf->SetHome(m_HomeNumber);
			cf->SetSelected(m_SelectedNumber);

			ssIfFolder *child = cf->GetChild(m_SelectedNumber);
			child->SetParent(cf);
			m_HomeNumber = 0;
			m_SelectedNumber = 0;
			m_CursorPos = 0;

			m_CurrentFolder = child;

			if (child->GetType() == ssIfFolder::TYPE_FOLDER) {
				RedrawRequest(REDRAW_SELECTER);
			} else if (child->GetType() == ssIfFolder::TYPE_NODE) {
				ssDriverConfig *cfg = dynamic_cast<ssDriverConfig *>(child);
				ASSERT(child != NULL);
				if (m_psdm->LoadDriver(cfg)) {
					ClearInfo();
					m_HomeTrack = 0;
					RedrawRequest(REDRAW_ALL);
				}
			}
		}
		break;
	}
}


void ssDisplay::TitleSelect(UINT _key)
{
	ssDriverConfig *cfg = dynamic_cast<ssDriverConfig *>(m_CurrentFolder);
	ASSERT(cfg != NULL);
	const int size = cfg->titlelist.size();
	int i;

	switch (_key) {
	case VK_DOWN:
		FolderSelectDownOne(size);
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_UP:
		FolderSelectUpOne(size);
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_NEXT:
		for (i = 0; i < MAX_SELECT; i++) {
			FolderSelectDownOne(size);
		}
		RedrawRequest(REDRAW_SELECTER);
		break;
	case VK_PRIOR:
		for (i = 0; i < MAX_SELECT; i++) {
			FolderSelectUpOne(size);
		}
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_HOME:
		m_HomeNumber = 0;
		m_SelectedNumber = 0;
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_END:
		m_HomeNumber = size - MAX_SELECT;
		if (m_HomeNumber < 0) {
			m_HomeNumber = 0;
		}
		m_SelectedNumber = size - 1;
		if (m_SelectedNumber < 0) {
			m_SelectedNumber = 0;
		}
		RedrawRequest(REDRAW_SELECTER);
		break;

	case VK_BACK:
	case VK_ESCAPE:
		{
			ssIfFolder *parent = m_CurrentFolder->GetParent();
			if (parent != NULL) {
				ssFolder *pf = dynamic_cast<ssFolder *>(parent);
				if (pf != NULL) {
					m_HomeNumber = pf->GetHome();
					m_SelectedNumber = pf->GetSelected();
					m_CurrentFolder = pf;
					RedrawRequest(REDRAW_SELECTER);
				}
			}
		}
		break;
	case VK_RETURN:
	case VK_SPACE:
		if (m_SelectedNumber < size) {
			ssSoundDriver *sd = m_psdm->GetCurrentDriver();
			if (sd != NULL) {
				m_psdm->ResetTimeCount();
				sd->Play(cfg->titlelist[m_SelectedNumber].code);
			}

			if (_key == VK_SPACE) {
				FolderSelectDownOne(size);
				RedrawRequest(REDRAW_SELECTER);
			}
		}
		break;
	case '0':
		{
			ssSoundDriver *sd = m_psdm->GetCurrentDriver();
			if (sd != NULL) {
				sd->Play(0);
			}
		}
		break;
	case 'P':
		{
			ssSoundDriver *sd = m_psdm->GetCurrentDriver();
			if (sd != NULL) {
				sd->Stop();
			}
		}
		break;
	}
}

void ssDisplay::MaskSelect(UINT _key)
{
	switch (_key) {
	case VK_TAB:
	case VK_BACK:
	case VK_ESCAPE:
		{
			m_KeyState = KEY_STATE_SELECTER;
			RedrawRequest(REDRAW_SELECTER | REDRAW_CURSOR);
		}
		break;
	case VK_DOWN:
		{
			const int track_count = m_psdm->GetTrackCount();
			if (m_CursorPos < track_count - 1 && m_CursorPos < MAX_TRACK - 1) {
				m_CursorPos++;
				RedrawRequest(REDRAW_CURSOR);
			} else {
				if (m_HomeTrack + MAX_TRACK < track_count) {
					m_HomeTrack++;
					RedrawRequest(REDRAW_ALL);
				}
			}
		}
		break;
	case VK_UP:
		{
			if (m_CursorPos > 0) {
				m_CursorPos--;
				RedrawRequest(REDRAW_CURSOR);
			} else {
				if (m_HomeTrack > 0) {
					m_HomeTrack--;
					RedrawRequest(REDRAW_ALL);
				}
			}
		}
		break;
	case VK_RETURN:
	case VK_SPACE:
		{
			int trk = m_HomeTrack + m_CursorPos;
			m_psdm->SetMask(trk, !m_psdm->GetMask(trk));
		}
		break;
	}
}

void ssDisplay::OnMove(int _x, int _y)
{
	if (!m_bFullScreen && m_bScreenChangeReady) {
		::GetWindowRect(m_hWnd, &m_rcWindow);
#if 0
		char s[64];
		sprintf(s, "M(%d - %d) - (%d - %d)\n",
			m_rcWindow.left,
			m_rcWindow.top,
			m_rcWindow.right,
			m_rcWindow.bottom);
		OutputDebugString(s);
	} else {
		OutputDebugString("M:Ignore\n");
#endif
	}
}

void ssDisplay::DropFile(const string &_fname)
{
	m_ConfigSingleFile->SetFile(_fname);
	m_ConfigSingleFile->ClearTitle();

	if ( m_psdm->LoadDriverForSingleFile(m_ConfigSingleFile)) {

		if (m_CurrentFolder != m_ConfigSingleFile) {
			ssIfFolder *folder = m_CurrentFolder;
			if (folder->GetType() == ssIfFolder::TYPE_NODE) {
				folder = folder->GetParent();
			}
			m_ConfigSingleFile->SetParent(folder);
		}
		m_HomeNumber = 0;
		m_SelectedNumber = 0;

		m_CurrentFolder = m_ConfigSingleFile;
		ClearInfo();
		m_HomeTrack = 0;
		m_KeyState = KEY_STATE_SELECTER;
		RedrawRequest(REDRAW_ALL);
	}
}
