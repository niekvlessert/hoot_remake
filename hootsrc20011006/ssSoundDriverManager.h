#ifndef __SSSOUNDDRIVERMANAGER_H__
#define __SSSOUNDDRIVERMANAGER_H__

#include "ssSoundDriver.h"
#include "ssConfig.h"
#include "ssIfFolder.h"
#include "ssFolder.h"
#include "ssDriverConfig.h"
#include "ssBindConfig.h"
#include "ssSoundChip.h"
#include "ssTrackInfo.h"
#include "ssSoundStream.h"
#include "ssTimerManager.h"
#include "ssTimer.h"
#include "ssMutex.h"
#include "ssIfFft.h"

#include <mmsystem.h>
#include <dsound.h>

class ssDisplay;

class ssSoundDriverManager
{
public:
	enum {
		SPEED_NORMAL = 0,
		SPEED_SLOW,
		SPEED_FAST,
		SPEED_SILENT,
	};
public:
	static ssSoundDriverManager *Instance(void);
private:
	static void CleanUp(void);

protected:
	ssSoundDriverManager();

public:
	~ssSoundDriverManager();

	bool Initialize(HWND _hWnd = NULL);

	bool RegistTimer(ssTimer *_timer);
	bool RemoveTimer(ssTimer *_timer);

	const ssConfig &GetConfig(void) const;

	void MakeFolders(void);
	void DeleteFolders(void);
	ssIfFolder *GetRootFolder(void) const;

	void TimerProc(void);
	UINT ThreadProc(void);

	bool LoadDriver(ssDriverConfig *_config);
	bool LoadDriverForSingleFile(ssDriverConfig *_config);
	ssSoundDriver *GetCurrentDriver(void) const;
	bool RegisterSoundChip(ssSoundChip *_chip);
	ssSoundChip *GetSoundChip(int _index);
	int GetTrackCount(void) const;
	ssTrackInfo *GetInfo(int _track) const;

	void SetMask(int _track, bool _mask);
	bool GetMask(int _track);

	ssIfFft *GetFft(void) const;

	void Update(void);
	void UpdateScreen(void) const;
	ssDisplay *GetDisplay(void) const;

	void SetPlaySpeed(BYTE _speed);
	void ResetTimeCount(void);

	// for Filter
	bool GetFilter(void);
	void EnableFilter(bool _b);

private:
	static ssSoundDriverManager *m_Instance;

	void ClearSoundStreamList(void);

	void UpdateAllStream(DWORD _offset, DWORD _size);
	void MixAllStream(DWORD _offset, DWORD _size);
	DWORD Time2Size(double _time) const;
	double Size2Time(DWORD _size) const;

	void StreamCnv(short *_dst, const int *_src, const int _count);

	void Lock(void);
	void Unlock(void);

	HWND m_hWnd;
	ssSoundDriver *m_Driver;

	// for configuration
	ssConfig m_Config;
	vector<ssIfFolder *> m_DriverConfig;
	ssIfFolder *m_RootFolder;
	vector<ssFolder *> m_Folders;
	vector<ssBindConfig *> m_BindConfig;
	map<string, ssBindConfig *> m_ExtBindMap;

	// for chip management
	list<ssSoundStream *> m_StreamList;
	int *m_StreamBuffer;
	vector<ssTrackInfo *> track_info;
	struct MaskInfo
	{
		ssSoundChip *chip;
		int track;
		bool mask;
	};
	vector<MaskInfo> mask_info;

	// for timer
	ssTimerManager m_TimerManager;

	// for DirectSound;
	LPDIRECTSOUND m_lpDirectSound;
	LPDIRECTSOUNDBUFFER m_lpDSBPrimary;
	LPDIRECTSOUNDBUFFER m_lpDSBuffer;
	DWORD m_WriteCursor;

	// for scheduling
	double m_LastEventTime;
	BYTE m_PlaySpeed;

	// for multi-threading
	CEvent m_timer_ev;
	CEvent m_exit_ev;
	UINT m_timer_id;
	CWinThread *m_thread;
	ssMutex m_Mutex;

	// for Display
	ssDisplay *disp;
	ssIfFft *m_fft;
	int m_fft_count;

	// for time-count
	double m_TimeCount;

	// for Filter
	enum
	{
		maxorder = 8,			// LPF ÇÃ éüêî / 2 (pole ÇÃêî?)
	};

	void MakeFilter(int cutoff);
	int FilterL(int in);
	int FilterR(int in);

	int order;

	int fn[maxorder][4];
	int pl[maxorder][2], pr[maxorder][2];

	bool dofilter;
};

#endif // __SSSOUNDDRIVERMANAGER_H__
