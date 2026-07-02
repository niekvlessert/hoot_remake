#ifndef __SSSOUNDDRIVER_H__
#define __SSSOUNDDRIVER_H__

#include "ssDriverConfig.h"
#include "ssMutex.h"

// ドライバはこのクラスから派生させて作る。


class ssSoundDriver
{
public:
	// コンストラクタ/デストラクタ
	ssSoundDriver() {}
	virtual ~ssSoundDriver() {}

	// ドライバ初期化する。
	//   _config : そのドライバの情報
	virtual bool Initialize(ssDriverConfig *_config) = 0;

	// 指定秒数だけ時間を進める。
	//   _second : 秒数
	virtual void Execute(double _second) = 0;

	// タイマからの割り込みを実行する。
	//   _id : タイマの ID
	virtual void Interrupt(int _id) = 0;

	// 指定コードの曲の演奏を開始する。
	//   _code : 曲コード
	virtual bool Play(DWORD _code) = 0;

	// 演奏を停止する。
	virtual bool Stop(void) = 0;

protected:
	void Lock(void);
	void Unlock(void);

private:
	ssMutex m_Mutex;
};

inline void ssSoundDriver::Lock(void)
{
	m_Mutex.Lock();
}

inline void ssSoundDriver::Unlock(void)
{
	m_Mutex.Unlock();
}

#endif // __SSSOUNDDRIVER_H__
