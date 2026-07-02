//
// MainFrm.cpp : CMainFrame クラスの動作の定義を行います。
//

#include "stdafx.h"
#include "hoot.h"

#include "MainFrm.h"

#include "ssSoundDriverManager.h"
#include "ssSoundDriver.h"

#include "ssDisplay.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CMainFrame

IMPLEMENT_DYNAMIC(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	//{{AFX_MSG_MAP(CMainFrame)
	ON_WM_CREATE()
	ON_WM_SETFOCUS()
	ON_WM_KEYDOWN()
	ON_WM_KEYUP()
	ON_WM_PAINT()
	ON_WM_DESTROY()
	ON_COMMAND(ID_TOGGLEFULLSCREEN, OnTogglefullscreen)
	ON_WM_GETMINMAXINFO()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_WM_DROPFILES()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMainFrame クラスの構築/消滅

CMainFrame::CMainFrame()
{
	// TODO: この位置にメンバの初期化処理コードを追加してください。
	//m_UI = new ssDriverUI();
}

CMainFrame::~CMainFrame()
{
	//delete m_UI;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;
#if 0
	// フレームのクライアント領域全体を占めるビューを作成します。
	if (!m_wndView.Create(NULL, NULL, AFX_WS_DEFAULT_VIEW,
		CRect(0, 0, 0, 0), this, AFX_IDW_PANE_FIRST, NULL))
	{
		TRACE0("Failed to create view window\n");
		return -1;
	}
#endif

	return 0;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if( !CFrameWnd::PreCreateWindow(cs) )
		return FALSE;
	// TODO: この位置で CREATESTRUCT cs を修正して、Window クラスやスタイルを
	//       修正してください。

	cs.style = WS_OVERLAPPED | WS_CAPTION
		| WS_SYSMENU | WS_MINIMIZEBOX;
	//cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
	cs.dwExStyle |= WS_EX_CLIENTEDGE;
	//cs.lpszClass = AfxRegisterWndClass(0);
	cs.lpszClass = AfxRegisterWndClass(0,
		::LoadCursor(cs.hInstance, (LPCSTR)IDC_POINTER),
		0,
		::LoadIcon(cs.hInstance, (LPCSTR)IDR_MAINFRAME));

	RECT rc;
	rc.top = 0;
	rc.left = 0;
	rc.bottom = 480;
	rc.right = 640;
	::AdjustWindowRectEx(&rc, cs.style, TRUE, cs.dwExStyle);
	cs.cx = rc.right - rc.left;
	cs.cy = rc.bottom - rc.top;

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CMainFrame クラスの診断

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CFrameWnd::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CFrameWnd::Dump(dc);
}

#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CMainFrame メッセージ ハンドラ
void CMainFrame::OnSetFocus(CWnd* pOldWnd)
{
#if 0
	// ビュー ウィンドウにフォーカスを与えます。
	m_wndView.SetFocus();
#endif
}

BOOL CMainFrame::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
#if 0
	// ビューに最初にコマンドを処理する機会を与えます。
	if (m_wndView.OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
		return TRUE;
#endif

	// 処理されなかった場合にはデフォルトの処理を行います。
	return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}


void CMainFrame::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	ssDisplay *disp = psdm->GetDisplay();
	disp->OnKeyDown(nChar);

	CFrameWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CMainFrame::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	ssDisplay *disp = psdm->GetDisplay();
	disp->OnKeyUp(nChar);

	CFrameWnd::OnKeyUp(nChar, nRepCnt, nFlags);
}

void CMainFrame::OnPaint() 
{
	CPaintDC dc(this); // 描画用のデバイス コンテキスト
	
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
	//dc.FillSolidRect(0, 0, 640, 480, RGB(255,255,255));
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	psdm->UpdateScreen();
#if 0
	CString str;

	const ssDriverConfig &config = m_UI->GetSelectedDriver();
	str.Format("%s", config.name.c_str());
	dc.TextOut(0, 0, str);

	const ssDriverConfig::ssTitle &title = m_UI->GetSelectedTitle();
	str.Format("%02x : %s", title.code, title.name.c_str());
	dc.TextOut(0, 20, str);
#endif
	
	// 描画用メッセージとして CFrameWnd::OnPaint() を呼び出してはいけません
}

void CMainFrame::OnDestroy() 
{
	CFrameWnd::OnDestroy();
	
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
}


void CMainFrame::OnTogglefullscreen() 
{
	// TODO: この位置にコマンド ハンドラ用のコードを追加してください
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	ssDisplay *disp = psdm->GetDisplay();
	disp->SwitchFullScreen();
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	HWND hWnd = GetSafeHwnd();
	RECT rc;
	rc.top = 0;
	rc.left = 0;
	rc.bottom = 480;
	rc.right = 640;
	::AdjustWindowRectEx(&rc,
		::GetWindowLong(hWnd, GWL_STYLE),
		TRUE,
		::GetWindowLong(hWnd, GWL_EXSTYLE));
	lpMMI->ptMinTrackSize.x = rc.right - rc.left;
	lpMMI->ptMinTrackSize.y = rc.bottom - rc.top;
	lpMMI->ptMaxTrackSize.x = lpMMI->ptMinTrackSize.x;
	lpMMI->ptMaxTrackSize.y = lpMMI->ptMinTrackSize.y;
	
	//CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

void CMainFrame::OnMove(int x, int y) 
{
	//CFrameWnd::OnMove(x, y);
	
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	ssDisplay *disp = psdm->GetDisplay();
	disp->OnMove(x, y);
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) 
{
	CFrameWnd::OnSize(nType, cx, cy);
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
	
}

void CMainFrame::OnDropFiles(HDROP hDropInfo) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	//SetActiveWindow();
	SetForegroundWindow();
	UINT nFiles = ::DragQueryFile(hDropInfo, (UINT)-1, NULL, 0);

	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	ASSERT(psdm != NULL);
	ssDisplay *disp = psdm->GetDisplay();
	ASSERT(disp != NULL);

	if (nFiles > 0) {
		TCHAR szFileName[_MAX_PATH];
		::DragQueryFile(hDropInfo, 0, szFileName, _MAX_PATH);
		disp->DropFile(string(szFileName));
		//AfxMessageBox(szFileName);
		//psdm->PlayFile(szFileName);
	}
	::DragFinish(hDropInfo);
}
