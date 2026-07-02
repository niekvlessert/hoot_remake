//
// hoot.cpp : アプリケーション用クラスの機能定義を行います。
//

#include "stdafx.h"
#include "hoot.h"

#include "MainFrm.h"
#include "codeguru/HyperLink.h"

#include "ssDriverConfig.h"
#include "ssDriverBinder.h"
#include "ssSoundDriverManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CHootApp

BEGIN_MESSAGE_MAP(CHootApp, CWinApp)
	//{{AFX_MSG_MAP(CHootApp)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
		//        この位置に生成されるコードを編集しないでください。
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CHootApp クラスの構築

CHootApp::CHootApp()
{
	// TODO: この位置に構築用コードを追加してください。
	// ここに InitInstance 中の重要な初期化処理をすべて記述してください。
	m_hResourceInstance = NULL;
}

/////////////////////////////////////////////////////////////////////////////
// 唯一の CHootApp オブジェクト

CHootApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CHootApp クラスの初期化

BOOL CHootApp::InitInstance()
{
	AfxEnableControlContainer();

	// 標準的な初期化処理
	// もしこれらの機能を使用せず、実行ファイルのサイズを小さく
	// したければ以下の特定の初期化ルーチンの中から不必要なもの
	// を削除してください。

#ifdef _AFXDLL
	Enable3dControls();		// 共有 DLL の中で MFC を使用する場合にはここを呼び出してください。
#else
	Enable3dControlsStatic();	// MFC と静的にリンクしている場合にはここを呼び出してください。
#endif

	{
		bool bDumpDescription = false;

		int argc = __argc;
		char **argv = __argv;

		for (int i = 1; i < argc; i++) {
			string arg = argv[i];
			if (arg == "-list") {
				bDumpDescription = true;
			}
		}

		if (bDumpDescription) {
			ssDriverBinder::DumpDescription(string("drivers.xml"));
		}
	}

	// 設定が保存される下のレジストリ キーを変更します。
	// TODO: この文字列を、会社名または所属など適切なものに
	// 変更してください。
	//SetRegistryKey(_T("Local AppWizard-Generated Applications"));
	if (::GetFileAttributes(".\\hootdev.ini") != 0xffffffff) {
		m_pszProfileName= _tcsdup(_T(".\\hootdev.ini"));
	} else {
		m_pszProfileName= _tcsdup(_T(".\\hoot.ini"));
	}

	ssSoundDriverManager *psdm = ssSoundDriverManager::Instance();
	const ssConfig &config = psdm->GetConfig();

	if (config.language != "") {
		m_hResourceInstance = ::LoadLibrary(config.language.c_str());
		AfxSetResourceHandle(m_hResourceInstance);
	}

	// メイン ウインドウを作成するとき、このコードは新しいフレーム ウインドウ オブジェクトを作成し、
	// それをアプリケーションのメイン ウインドウにセットします

	CMainFrame* pFrame = new CMainFrame;
	m_pMainWnd = pFrame;

	// フレームをリソースからロードして作成します
	pFrame->LoadFrame(IDR_MAINFRAME,
		WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, NULL, 
		NULL);

	psdm->Initialize(pFrame->GetSafeHwnd());

	//pFrame->disp.Initialize(pFrame->m_hWnd);

	// メイン ウィンドウが初期化されたので、表示と更新を行います。
	pFrame->DragAcceptFiles(TRUE);
	pFrame->ShowWindow(SW_SHOW);
	pFrame->UpdateWindow();

	return TRUE;
}


int CHootApp::ExitInstance() 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
	AfxSetResourceHandle(m_hInstance);

	if (m_hResourceInstance != NULL) {
		::FreeLibrary(m_hResourceInstance);
	}

	return CWinApp::ExitInstance();
}

/////////////////////////////////////////////////////////////////////////////
// CHootApp message handlers





/////////////////////////////////////////////////////////////////////////////
// アプリケーションのバージョン情報で使われる CAboutDlg ダイアログ

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// ダイアログ データ
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	CHyperLink	m_HyperLink;
	//}}AFX_DATA

	// ClassWizard 仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV のサポート
	//}}AFX_VIRTUAL

// インプリメンテーション
protected:
	//{{AFX_MSG(CAboutDlg)
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	DDX_Control(pDX, IDC_HYPERLINK, m_HyperLink);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

// ダイアログを実行するためのアプリケーション コマンド
void CHootApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

/////////////////////////////////////////////////////////////////////////////
// CHootApp メッセージ ハンドラ

BOOL CAboutDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	// TODO: この位置に初期化の補足処理を追加してください
	
	m_HyperLink.ModifyLinkStyle(0, CHyperLink::StyleUseHover);

	return TRUE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
