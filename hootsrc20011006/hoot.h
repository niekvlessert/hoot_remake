//
// hoot.h : HOOT アプリケーションのメイン ヘッダー ファイル
//

#if !defined(AFX_HOOT_H__57233544_8FC9_11D3_B7C5_005004B4E2CB__INCLUDED_)
#define AFX_HOOT_H__57233544_8FC9_11D3_B7C5_005004B4E2CB__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // メイン シンボル

/////////////////////////////////////////////////////////////////////////////
// CHootApp:
// このクラスの動作の定義に関しては hoot.cpp ファイルを参照してください。
//

class CHootApp : public CWinApp
{
public:
	CHootApp();

// オーバーライド
	// ClassWizard は仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(CHootApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	//}}AFX_VIRTUAL

// インプリメンテーション

public:
	//{{AFX_MSG(CHootApp)
	afx_msg void OnAppAbout();
		// メモ - ClassWizard はこの位置にメンバ関数を追加または削除します。
		//        この位置に生成されるコードを編集しないでください。
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	HINSTANCE m_hResourceInstance;
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。

#endif // !defined(AFX_HOOT_H__57233544_8FC9_11D3_B7C5_005004B4E2CB__INCLUDED_)
