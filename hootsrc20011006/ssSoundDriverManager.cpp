#include "StdAfx.h"
#include "ssSoundDriverManager.h"
#include "ssSoundChip.h"
#include "ssTimerManager.h"
#include "ssTimer.h"
#include "ssIfFolder.h"
#include "ssFolder.h"
#include "ssDriverConfig.h"
#include "ssConfigLoader.h"
#include "ssDriverBinder.h"
#include "ssDisplay.h"
#include "ssIfFft.h"
#include "ssFft.h"
#include "ssUnZip.h"

#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dsound.lib")

// 時間管理したり波形を生成したりするコア部分


#define SAFERELEASE(p) {if (p) {(p)->Release(); p = NULL;}}

static inline const int maxint(int a, int b)
{
	const int x = a - b;
	const int y = x >> 31;
	return a - (x&y);
}


static inline const int minint(int a, int b)
{
	const int x = a - b;
	const int y = x >> 31;
	return (x&y) + b;
}

static const inline DWORD Byte2Sample(DWORD _b)
{
	return _b / 4;
}

static const inline DWORD Sample2Byte(DWORD _s)
{
	return _s * 4;
}

static void ClearDirectSoundBuffer(LPDIRECTSOUNDBUFFER lpDSB, DWORD pos, DWORD size)
{
	HRESULT hr;
	void *ptr1, *ptr2;
	DWORD size1, size2;

	hr = lpDSB->Lock(pos, size,
		&ptr1, &size1, &ptr2, &size2, 0);

	if (SUCCEEDED(hr)) {
		memset(ptr1, 0, size1);
		if (ptr2) {
			memset(ptr2, 0, size2);
		}
		lpDSB->Unlock(ptr1, size1, ptr2, size2);
	} else {
		TRACE("Lock Fail %08x:%d\n", pos, size);
	}
}

ssSoundDriverManager *ssSoundDriverManager::m_Instance = NULL;

ssSoundDriverManager *ssSoundDriverManager::Instance(void)
{
	if (m_Instance == NULL) {
		m_Instance = new ssSoundDriverManager;
	}
	return m_Instance;
}

void ssSoundDriverManager::CleanUp(void)
{
	if (m_Instance != NULL) {
		delete m_Instance;
		m_Instance = NULL;
	}
}

ssSoundDriverManager::ssSoundDriverManager()
{
	atexit(CleanUp);

	disp = new ssDisplay();

	m_Config.Load(string("config.xml"));
	ssUnZip::SetSearchPath(&m_Config.rompath);

	ssConfigLoader::Load(m_Config.title_file, m_DriverConfig);
	//ssConfigLoaderg::Dump(m_DriverConfig);

	MakeFolders();

	m_fft = new ssFft(m_Config.fft_size, 16, 11025,
					  m_Config.sampling_rate, m_Config.fft_winfnc);
	m_fft_count = 0;

	m_Driver = NULL;
	m_PlaySpeed = SPEED_NORMAL;

	m_TimeCount = 0.0;

	m_StreamBuffer = new int[Sample2Byte(m_Config.stream_buffer_count)];
}

ssSoundDriverManager::~ssSoundDriverManager()
{
	if (m_timer_id != 0) {
		::timeKillEvent(m_timer_id);
		m_timer_id = 0;
	}
	if (m_thread != NULL) {
		m_exit_ev.SetEvent();
		::WaitForSingleObject(m_thread->m_hThread, INFINITE);
	}

	Lock();

	if (m_Driver != NULL) {
		delete m_Driver;
		m_Driver = NULL;
	}

	ClearSoundStreamList();

	DeleteFolders();
	ssConfigLoader::Free(m_DriverConfig);

	SAFERELEASE(m_lpDSBuffer);
	SAFERELEASE(m_lpDSBPrimary);
	SAFERELEASE(m_lpDirectSound);

	delete[] m_StreamBuffer;

	delete m_fft;
	delete disp;
}

const ssConfig &ssSoundDriverManager::GetConfig(void) const
{
	return m_Config;
}

void ssSoundDriverManager::MakeFolders(void)
{
	// make folders from m_DriverConfig;
	map<string, ssFolder *> drivermap;

	ssFolder *root = new ssFolder();
	m_Folders.push_back(root);
	m_RootFolder = root;

	ssFolder *all = new ssFolder();
	m_Folders.push_back(all);
	root->AddFolder(all);
	all->SetName("- all -");

	vector<ssIfFolder *>::const_iterator i;
	for (i = m_DriverConfig.begin(); i != m_DriverConfig.end(); i++) {
		ssIfFolder *folder = *i;
		ssDriverConfig *drvconfig = dynamic_cast<ssDriverConfig *>(folder);
		ssBindConfig *bindconfig = dynamic_cast<ssBindConfig *>(folder);
		if (drvconfig != NULL) {
			// game
			string &driver = drvconfig->driver_majortype;
			string &type = drvconfig->driver_subtype;
			string fulltype = driver + '/' + type;

			all->AddFolder(drvconfig);

			ssFolder *df = drivermap[driver];
			if (df == NULL) {
				df = new ssFolder();
				m_Folders.push_back(df);
				drivermap[driver] = df;

				df->SetName(driver);
				root->AddFolder(df);

				ssFolder *dall = new ssFolder();
				m_Folders.push_back(dall);
				df->AddFolder(dall);
				dall->SetName("- all -");

			}
			{
				ssIfFolder *dall = df->GetChild(0);
				ASSERT(dall != NULL);
				ssFolder *dallf = dynamic_cast<ssFolder *>(dall);
				ASSERT(dallf != NULL);
				dallf->AddFolder(drvconfig);
			}

			ssFolder *tf = drivermap[fulltype];
			if (tf == NULL) {
				tf = new ssFolder();
				m_Folders.push_back(tf);
				drivermap[fulltype] = tf;

				tf->SetName(type);
				df->AddFolder(tf);
			}
			tf->AddFolder(drvconfig);
		} else if (bindconfig != NULL) {
			// bind
			m_BindConfig.push_back(bindconfig);

			const int count = bindconfig->GetExtCount();
			for (int idx = 0; idx < count; idx++) {
				string ext = bindconfig->GetExt(idx);
				m_ExtBindMap[ext] = bindconfig;
			}
		}
	}
}

static void DeleteFolder(ssFolder *_folder)
{
	delete _folder;
}

void ssSoundDriverManager::DeleteFolders()
{
	for_each(m_Folders.begin(), m_Folders.end(), DeleteFolder);
	m_Folders.clear();
	m_RootFolder = NULL;
}

ssIfFolder *ssSoundDriverManager::GetRootFolder(void) const
{
	return m_RootFolder;
}

static UINT ThreadProc(LPVOID lpParam)
{
	ssSoundDriverManager *psdm = (ssSoundDriverManager *)lpParam;

	return psdm->ThreadProc();
}

static void CALLBACK TimerProc(UINT uID, UINT uMsg, DWORD dwUser,
							  DWORD dw1, DWORD dw2)
{
	ssSoundDriverManager *psdm = (ssSoundDriverManager *)dwUser;

	psdm->TimerProc();
}

bool ssSoundDriverManager::Initialize(HWND _hWnd)
{
	HRESULT hr;

	m_hWnd = _hWnd;

	// DirectSound の初期化
	hr = DirectSoundCreate(NULL, &m_lpDirectSound, NULL);
	hr = m_lpDirectSound->SetCooperativeLevel(m_hWnd, DSSCL_PRIORITY);
	//hr = m_lpDirectSound->SetCooperativeLevel(m_hWnd, DSSCL_EXCLUSIVE);

	// プライマリバッファを取得して出力フォーマットを設定
	DSBUFFERDESC dsbdesc;
	ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	hr = m_lpDirectSound->CreateSoundBuffer(&dsbdesc, &m_lpDSBPrimary, NULL);
	if (FAILED(hr)) {
		::OutputDebugString("CreatePrimarySoundBuffer failed.\n");
		m_lpDSBPrimary = NULL;
	}

	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = m_Config.sampling_rate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = wfx.wBitsPerSample / 8 * wfx.nChannels;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	hr = m_lpDSBPrimary->SetFormat(&wfx);
	if (FAILED(hr)) {
		::OutputDebugString("SetFormat failed.\n");
	}

	// 出力用セカンダリバッファを作成
	ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = 0
		//| DSBCAPS_CTRLFREQUENCY
		//| DSBCAPS_CTRLPAN
		//| DSBCAPS_CTRLVOLUME
		| DSBCAPS_GETCURRENTPOSITION2	// Always a good idea
		| DSBCAPS_GLOBALFOCUS			// Allows background playing
		//| DSBCAPS_CTRLPOSITIONNOTIFY	// Needed for notification
		| 0;
	dsbdesc.dwBufferBytes = wfx.nBlockAlign * m_Config.stream_buffer_count;
	dsbdesc.lpwfxFormat = &wfx;
	dsbdesc.guid3DAlgorithm = GUID_NULL;
 	hr = m_lpDirectSound->CreateSoundBuffer(&dsbdesc, &m_lpDSBuffer, NULL);
	if (FAILED(hr)) {
		::OutputDebugString("CreateSecondarySoundBuffer failed.\n");
		m_lpDSBuffer = NULL;
	}
	ClearDirectSoundBuffer(m_lpDSBuffer, 0, dsbdesc.dwBufferBytes);
	//m_lpDSBuffer->SetFrequency(44100);

	disp->Initialize(_hWnd, this);

	// タイマ/スレッドの初期化
	m_timer_id = ::timeSetEvent(m_Config.time_slice * 1000.0, m_Config.time_slice * 1000.0/ 2,
		::TimerProc, (DWORD)this, TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
	m_thread = ::AfxBeginThread(::ThreadProc, this,
		THREAD_PRIORITY_ABOVE_NORMAL, 0, 0, NULL);
	::SetPriorityClass(m_thread, HIGH_PRIORITY_CLASS);

	// フィルタ関連

	dofilter = m_Config.lpf ? true : false;
	int lpf_cutoff = m_Config.lpf_cutoff;
	int lpf_order = m_Config.lpf_order;

	if (lpf_cutoff < 1000) {
		lpf_cutoff = 1000;
	}
	if (lpf_cutoff > m_Config.sampling_rate / 2) {
		lpf_cutoff = m_Config.sampling_rate / 2;
	}
	if (lpf_order < 1) {
		lpf_order = 1;
	}
	if (lpf_order > maxorder) {
		lpf_order = maxorder;
	}
	order = lpf_order;
	MakeFilter(lpf_cutoff);
	return true;
}

bool ssSoundDriverManager::RegistTimer(ssTimer *_timer)
{
	return m_TimerManager.Register(_timer);
}

bool ssSoundDriverManager::RemoveTimer(ssTimer *_timer)
{
	return m_TimerManager.Remove(_timer);
}

void ssSoundDriverManager::Lock(void)
{
	m_Mutex.Lock();
}

void ssSoundDriverManager::Unlock(void)
{
	m_Mutex.Unlock();
}

void ssSoundDriverManager::TimerProc(void)
{
	m_timer_ev.SetEvent();
}

UINT ssSoundDriverManager::ThreadProc(void)
{
	HANDLE ev[] = {
		(HANDLE)m_timer_ev,
		(HANDLE)m_exit_ev
	};

	for (;;) {
		switch (::WaitForMultipleObjects(2, ev, FALSE, INFINITE)) {
		case WAIT_OBJECT_0 + 0:
			{
				Lock();
				//TRACE("Timer\n");
				Update();
				InvalidateRect(m_hWnd, NULL, FALSE);
				disp->Update();
				Unlock();
			}
			break;
		case WAIT_OBJECT_0 + 1:
			TRACE("Exit\n");
			return 0;
			break;
		}
		Sleep(0);
	}
}

// 返す値はサンプル数であって、バイト数ではない。
// ステレオも考慮していない。
DWORD ssSoundDriverManager::Time2Size(double _time) const
{
	DWORD size;

	size = (DWORD)((double)m_Config.sampling_rate * _time + 0.5);
	return size;
}

double ssSoundDriverManager::Size2Time(DWORD _size) const
{
	double time;

	time = (double)((double)_size / (double)m_Config.sampling_rate);
	return time;
}

void ssSoundDriverManager::ClearSoundStreamList(void)
{
	list<ssSoundStream *>::const_iterator i;
	for (i = m_StreamList.begin(); i != m_StreamList.end(); i++) {
		delete *i;
	}
	m_StreamList.clear();
}

void ssSoundDriverManager::UpdateAllStream(DWORD _offset, DWORD _size)
{
	short *buffarray[ssSoundStream::MAX_BUFF];
	list<ssSoundStream *>::const_iterator soundstream;
	for (soundstream = m_StreamList.begin(); soundstream != m_StreamList.end(); soundstream++) {
		for (int i = 0; i < (*soundstream)->num_buff; i++) {
			if ((*soundstream)->flag[i] & ssSoundStream::F_STEREO) {
				buffarray[i] = (*soundstream)->buff[i] + _offset * 2;
			} else if ((*soundstream)->flag[i] & ssSoundStream::F_STEREO_LONG) {
				buffarray[i] = (*soundstream)->buff[i] + _offset * 2 * 2;
			} else if ((*soundstream)->flag[i] & ssSoundStream::F_STEREO_SEPARATE_LONG) {
				buffarray[i] = (*soundstream)->buff[i] + _offset * 2 * 2;
			} else {
				buffarray[i] = (*soundstream)->buff[i] + _offset;
			}
		}
		(*soundstream)->chip->Update(buffarray, _size);
	}
}

static void MixStereo(int *_dst, const short *_src, const DWORD _size, const short _vol)
{
	int *dst = _dst;
	const short *src = _src;
	const DWORD count = _size * 2;
	for (int i = 0; i < count; i++) {
		*dst++ += *src++ * _vol;
	}
}

static void MixStereoLong(int *_dst, const int *_src, const DWORD _size, const short _vol)
{
	int *dst = _dst;
	const int *src = _src;
	const DWORD count = _size * 2;
	for (int i = 0; i < count; i++) {
		*dst++ += *src++ * _vol;
	}
}

static void MixMono(int *_dst, const short *_src, const DWORD _size, const short _vol)
{
	int *dst = _dst;
	const short *src = _src;
	const DWORD count = _size;
	for (int i = 0; i < count; i++) {
		int data = *src++ * _vol;
		*dst++ += data;
		*dst++ += data;
	}
}

static void MixSingle(int *_dst, const short *_src, const DWORD _size, const short _vol)
{
	int *dst = _dst;
	const short *src = _src;
	const DWORD count = _size;
	for (int i = 0; i < count; i++) {
		int data = *src++ * _vol;
		*dst++ += data;
		dst++;
	}
}

static void MixSeparate(int *_dst, const short *_src, const DWORD _size,
						const short _vol, const short _vol2)
{
	int *dst = _dst;
	const short *src = _src;
	const DWORD count = _size;
	for (int i = 0; i < count; i++) {
		int data = *src++;
		*dst++ += data * _vol;
		*dst++ += data * _vol2;
	}
}

static void MixStereoSeparateLong(int *_dst, const int *_src, const DWORD _size,
								  const short _vol, const short _vol2,
								  const short _vol3, const short _vol4)
{
	int *dst = _dst;
	const int *src = _src;
	const DWORD count = _size;
	for (int i = 0; i < count; i++) {
		int data1 = *src++;
		int data2 = *src++;
		*dst++ += data1 * _vol  + data2 * _vol3;
		*dst++ += data1 * _vol2 + data2 * _vol4;
	}
}

void ssSoundDriverManager::MixAllStream(DWORD _offset, DWORD _size)
{
	list<ssSoundStream *>::const_iterator soundstream;

	memset(m_StreamBuffer + _offset*2, 0, Sample2Byte(_size)*2);
	for (soundstream = m_StreamList.begin(); soundstream != m_StreamList.end(); soundstream++) {
		for (int i = 0; i < (*soundstream)->num_buff; i++) {
			//const WORD flag = (*soundstream)->flag[i];
			const WORD flag = (*soundstream)->chip->GetBufferFlag(i);
			const short vol = (*soundstream)->chip->GetVolume(i, 0);
			short *const buff = (*soundstream)->buff[i];
			int *dst = m_StreamBuffer + _offset*2;
			const short *src = buff + _offset;
			if (flag == ssSoundStream::F_STEREO) {
				MixStereo(dst, src + _offset, _size, vol);
			} else if (flag == ssSoundStream::F_STEREO_LONG) {
				const int *srcl = ((int *)buff) + _offset*2;
				MixStereoLong(dst, srcl, _size, vol);
			} else if (flag == ssSoundStream::F_MONO) {
				MixMono(dst, src, _size, vol);
			} else if (flag == ssSoundStream::F_LEFT) {
				MixSingle(dst, src, _size, vol);
			} else if (flag == ssSoundStream::F_RIGHT) {
				MixSingle(dst + 1, src, _size, vol);
			} else if (flag == ssSoundStream::F_SEPARATE) {
				const short vol2 = (*soundstream)->chip->GetVolume(i, 1);
				MixSeparate(dst, src, _size, vol, vol2);
			} else if (flag == ssSoundStream::F_STEREO_SEPARATE_LONG) {
				const short vol2 = (*soundstream)->chip->GetVolume(i, 1);
				const short vol3 = (*soundstream)->chip->GetVolume(i, 2);
				const short vol4 = (*soundstream)->chip->GetVolume(i, 3);
				const int *srcl = ((int *)buff) + _offset*2;
				MixStereoSeparateLong(dst, srcl, _size, vol, vol2, vol3, vol4);
			}
		}
	}
}

bool ssSoundDriverManager::LoadDriver(ssDriverConfig *_config)
{
	bool ret = false;
	ssDriverBinder *binder = ssDriverBinder::Instance();
	//ssTimerManager *tm = ssTimerManager::Instance();

	Lock();

	// ストリーミング停止
	m_lpDSBuffer->Stop();
	m_lpDSBuffer->SetCurrentPosition(0);
	m_WriteCursor = 0;
	ClearDirectSoundBuffer(m_lpDSBuffer, 0, Sample2Byte(m_Config.stream_buffer_count));

	ClearSoundStreamList();
	track_info.clear();
	mask_info.clear();

	if (m_Driver != NULL) {
		delete m_Driver;
		m_Driver = NULL;
	}

	if (_config != NULL) {
		// ドライバのインスタンス取得
		m_Driver = binder->CreateDriver(_config);

		if (m_Driver == NULL) {
			ret = false;
		} else {

			m_LastEventTime = 0.0;
			m_TimerManager.ResetAllTimers();

			m_Driver->Initialize(_config);

			// 最初の分を合成
			Update();

			// ストリーミング開始
			m_lpDSBuffer->Play(0, 0, DSBPLAY_LOOPING);
			m_lpDSBuffer->SetCurrentPosition(0);

			ret = true;
		}
	} else {
		ret = false;
	}

	Unlock();

	return ret;
}

bool ssSoundDriverManager::LoadDriverForSingleFile(ssDriverConfig *_config)
{

	{
		const char *path;
		char drive[_MAX_DRIVE];
		char dir[_MAX_DIR];
		char fname[_MAX_FNAME];
		char ext[_MAX_EXT];

		path = _config->archive.c_str();
		_splitpath(path, drive, dir, fname, ext);
		CharLower(ext);

		string key = ext + 1;
		if (m_ExtBindMap.count(key) == 0) {
			return false;
		}

		ssBindConfig *config = m_ExtBindMap[key];

		_config->SetBaseConfig(config);
	}

	return LoadDriver(_config);
}

ssSoundDriver *ssSoundDriverManager::GetCurrentDriver(void) const
{
	return m_Driver;
}

bool ssSoundDriverManager::RegisterSoundChip(ssSoundChip *_chip)
{
	m_StreamList.push_back(new ssSoundStream(_chip));

	MaskInfo mask;
	mask.chip = _chip;
	mask.mask = false;

	const int track = _chip->GetTrackCount();
	for (int i = 0; i < track; i++) {
		track_info.push_back(_chip->GetInfo(i));
		mask.track = i;
		mask_info.push_back(mask);
	}

	return true;
}

ssSoundChip *ssSoundDriverManager::GetSoundChip(int _index)
{
	int index = 0;
	list<ssSoundStream *>::const_iterator soundstream;
	for (soundstream = m_StreamList.begin(); soundstream != m_StreamList.end(); soundstream++) {
		if (index == _index) {
			return (*soundstream)->chip;
		}
		index++;
	}
	return NULL;
}

int ssSoundDriverManager::GetTrackCount(void) const
{
	return track_info.size();
}

ssTrackInfo *ssSoundDriverManager::GetInfo(int _track) const
{
	if (_track < track_info.size()) {
		return track_info[_track];
	}
	return NULL;
}

void ssSoundDriverManager::SetMask(int _track, bool _mask)
{
	if (_track < mask_info.size()) {
		ssSoundChip *chip = mask_info[_track].chip;
		int track = mask_info[_track].track;
		DWORD mask = chip->GetMask();
		if (_mask) {
			mask = mask | (1 << track);
		} else {
			mask = mask & ~(1 << track);
		}
		chip->SetMask(mask);

		mask_info[_track].mask = _mask;
	}
}

bool ssSoundDriverManager::GetMask(int _track)
{
	if (_track < mask_info.size()) {
		return mask_info[_track].mask;
	}

	return true;
}

ssIfFft *ssSoundDriverManager::GetFft(void) const
{
	return m_fft;
}

static inline short Cut8(const int _val)
{
	const int val = _val >> 8;
	return maxint(-0x8000, minint(val, 0x7fff));
}

static inline short Cut3(const int _val)
{
	const int val = _val;
	if (val > 0x7fff * 8) {
		return 0x7fff;
	} else if (val < -0x8000 * 8) {
		return -0x8000;
	} else {
		return (short)(val >> 3);
	}
}

inline void ssSoundDriverManager::StreamCnv(short *_dst, const int *_src, const int _count)
{
	const int count = _count;
	short *dst = _dst;
	const int *src = _src;
	if (!dofilter) {
		for (int i = 0; i < count; i++) {
			*dst++ = Cut8(*src++);
		}
	} else {
		for (int i = 0; i < count / 2; i++) {
			*dst++ = Cut3(FilterL(*src++>>5));
			*dst++ = Cut3(FilterR(*src++>>5));
		}
	}
}

void ssSoundDriverManager::Update(void)
{
	if (m_Driver == NULL) {
		return;
	}

	const int sampling_rate = m_Config.sampling_rate;
	const double stream_forward = m_Config.stream_forward;
	const int stream_buffer_count = m_Config.stream_buffer_count;

	DWORD lm_WriteCursor = m_WriteCursor;

	//ssTimerManager *TimerManager = ssTimerManager::Instance();

	// 更新するべきサンプル数
	int RestUpdateSize;
	int UpdateSize;

	{
		const DWORD BufferSize = Sample2Byte(stream_buffer_count);
		DWORD PlayCursor;
		DWORD WriteCursor;

		m_lpDSBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor);
		const DWORD FillCursor = (PlayCursor + Sample2Byte((int)((double)sampling_rate * stream_forward))) % BufferSize;

		RestUpdateSize = Byte2Sample((FillCursor + BufferSize - lm_WriteCursor) % BufferSize);
		if (
			(FillCursor >= lm_WriteCursor && lm_WriteCursor >= WriteCursor && PlayCursor <= FillCursor)
			|| (FillCursor <= lm_WriteCursor && lm_WriteCursor >= WriteCursor && PlayCursor >= FillCursor)
			|| (FillCursor >= lm_WriteCursor && lm_WriteCursor <= WriteCursor && PlayCursor >= FillCursor)
			|| (FillCursor >= lm_WriteCursor && lm_WriteCursor >= WriteCursor && PlayCursor >= FillCursor)
			) {
		} else {
			TRACE("Buffer Over Run(%06x,%06x,%06x,%06x)(%06x+%06x)\n",
				  PlayCursor, WriteCursor, lm_WriteCursor, FillCursor,
				  BufferSize, Sample2Byte((int)((double)sampling_rate * stream_forward)));
			lm_WriteCursor = WriteCursor;
		}
		UpdateSize = Byte2Sample((FillCursor + BufferSize - lm_WriteCursor) % BufferSize);
	}

	const int UpdateOffset = RestUpdateSize - UpdateSize;

	DWORD WriteOffset = 0;

	while (RestUpdateSize > 0) {
		ssTimer *Timer;
		double EventTime;

		Timer = m_TimerManager.GetNextEvent();
		if (Timer != NULL) {
			EventTime = Timer->GetNextEventTime();
			if (EventTime < m_LastEventTime) {
				// すでにイベント時間を過ぎている場合はそれが追い越すまでイベント発行
				bool tenable = Timer->InvokeHandlerBegin();
				Timer->Update();
				Timer->InvokeHandlerEnd();
				if (tenable) {
					m_Driver->Interrupt(Timer->GetID());
				}
				TRACE("oops\n");
				continue;
			}
		} else {
			EventTime = m_LastEventTime + Size2Time(Byte2Sample(RestUpdateSize));
		}

		double NextInterval = EventTime - m_LastEventTime;
		DWORD NextUpdateSize = Time2Size(NextInterval);
		if (NextUpdateSize == 0) NextUpdateSize = 1;

		if (m_PlaySpeed == SPEED_SLOW) {
			NextUpdateSize *= 4;
		} else if (m_PlaySpeed == SPEED_FAST) {
			NextUpdateSize /= 4;
		}
		//TRACE("N:%f(%d)\n", NextInterval, NextUpdateSize);

		double dur = EventTime - m_LastEventTime;

		if (NextUpdateSize > RestUpdateSize) {
			// 最後
			NextUpdateSize = RestUpdateSize;
			NextInterval = Size2Time(NextUpdateSize);
			m_LastEventTime += NextInterval;

			m_Driver->Execute(NextInterval);
			m_TimerManager.SetCurrentTime(m_LastEventTime);

			UpdateAllStream(WriteOffset, NextUpdateSize);
			MixAllStream(WriteOffset, NextUpdateSize);
			break;
		}

		m_LastEventTime = EventTime;

		if (Timer == NULL) {
			m_Driver->Execute(NextInterval);
			m_TimerManager.SetCurrentTime(EventTime);

			UpdateAllStream(WriteOffset, NextUpdateSize);
			MixAllStream(WriteOffset, NextUpdateSize);
		} else {
			m_Driver->Execute(NextInterval);
			m_TimerManager.SetCurrentTime(EventTime);

			UpdateAllStream(WriteOffset, NextUpdateSize);
			MixAllStream(WriteOffset, NextUpdateSize);

			bool tenable = Timer->InvokeHandlerBegin();

			Timer->Update();
			Timer->InvokeHandlerEnd();
			if (tenable) {
				m_Driver->Interrupt(Timer->GetID());
			}
		}

		RestUpdateSize -= NextUpdateSize;
		WriteOffset += NextUpdateSize;
	}

	{
		HRESULT hr;
		void *ptr1, *ptr2;
		DWORD size1, size2;

		const int size = UpdateSize;
		const int *const StreamBuffer = m_StreamBuffer + UpdateOffset/2;

		hr = m_lpDSBuffer->Lock(lm_WriteCursor, Sample2Byte(size),
			&ptr1, &size1, &ptr2, &size2, 0);

		if (SUCCEEDED(hr)) {
			//memcpy(ptr1, m_StreamBuffer, size1);
			if (m_PlaySpeed == SPEED_SILENT) {
				memset(ptr1, 0, size1);
			} else {
				StreamCnv((short *)ptr1, StreamBuffer, size1/2);
			}

			if (ptr2) {
				//memcpy(ptr2, m_StreamBuffer + size1/2, size2);
				if (m_PlaySpeed == SPEED_SILENT) {
					memset(ptr2, 0, size2);
				} else {
					StreamCnv((short *)ptr2, StreamBuffer + size1/2, size2/2);
				}
			}

			m_lpDSBuffer->Unlock(ptr1, size1, ptr2, size2);
		} else {
			TRACE("Lock Fail %08x:%d\n", lm_WriteCursor, Sample2Byte(size));
		}

		lm_WriteCursor += Sample2Byte(size);
		lm_WriteCursor %= Sample2Byte(stream_buffer_count);
	}

	m_WriteCursor = lm_WriteCursor;

	m_fft->Input(m_StreamBuffer, UpdateSize);
	if (m_fft_count >= m_Config.fft_skip) {
		m_fft->Calc();
		m_fft_count = 0;
	} else {
		m_fft_count++;
	}
}

void ssSoundDriverManager::UpdateScreen(void) const
{
	disp->Redraw();
	disp->Flip();
}

ssDisplay *ssSoundDriverManager::GetDisplay(void) const
{
	return disp;
}

void ssSoundDriverManager::SetPlaySpeed(BYTE _speed)
{
	m_PlaySpeed = _speed;
}

void ssSoundDriverManager::ResetTimeCount(void)
{
	m_TimeCount = 0.0;
}


// ---------------------------------------------------------------------------
//	バタワース特性 IIR LPF
//
#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

//#define F	4096
#define F	1024
#define FX(f)	int(f * F)

inline int ssSoundDriverManager::FilterL(int o)
{
	for (int j=0; j<order; j++)
	{
		int p = o + (pl[j][0] * fn[j][0] + pl[j][1] * fn[j][1]) / F;
		o = (p * fn[j][2] + pl[j][0] * fn[j][3] + pl[j][1] * fn[j][2]) / F;
		pl[j][1] = pl[j][0], pl[j][0] = p;
	}
	return o;
}

inline int ssSoundDriverManager::FilterR(int o)
{
	for (int j=0; j<order; j++)
	{
		int p = o + (pr[j][0] * fn[j][0] + pr[j][1] * fn[j][1]) / F;
		o =  (p * fn[j][2] + pr[j][0] * fn[j][3] + pr[j][1] * fn[j][2]) / F;
		pr[j][1] = pr[j][0], pr[j][0] = p;
	}
	return o;
}

void ssSoundDriverManager::MakeFilter(int fc)
{
	double wa = tan(M_PI * fc / m_Config.sampling_rate);
	double wa2 = wa*wa;

	int j;
	int n = 1;

	for (j=0; j<order; j++)
    {
		double zt = cos(n * M_PI / 4 / order);
		double ia0j = 1. / (1. + 2. * wa * zt + wa2);
		
		fn[j][0] = FX(-2. * (wa2 - 1.) * ia0j);
		fn[j][1] = FX(-(1. - 2. * wa * zt + wa2) * ia0j);
		fn[j][2] = FX(wa2 * ia0j);
		fn[j][3] = FX(2. * wa2 * ia0j);
		n += 2;
    }

	for (j=0; j<order; j++)
	{
		pl[j][0] = pl[j][1] =
		pr[j][0] = pr[j][1] = 0;
	}
}


bool ssSoundDriverManager::GetFilter(void)
{
	return dofilter;
}

void ssSoundDriverManager::EnableFilter(bool _b)
{
	dofilter = _b;
	if (dofilter) {
		for (int j=0; j<order; j++) {
			pl[j][0] = pl[j][1] =
				pr[j][0] = pr[j][1] = 0;
		}
	}
}
