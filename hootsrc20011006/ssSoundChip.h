#ifndef __SSSOUNDCHIP_H__
#define __SSSOUNDCHIP_H__

#include "ssTrackInfo.h"

// 音源はこのクラスから派生させて作る。


class ssSoundChip
{
public:
	// ストリームの数を返す。
	virtual int GetBufferCount(void) const = 0;

	// 指定した番号のストリームの種類を返す。
	// 戻り値は、ssSoundStream.h 参照。
	//   _b : ストリーム番号
	virtual WORD GetBufferFlag(int _b) const = 0;

	// 指定したストリーム、チャンネルのミキシングレベルを設定/取得する。
	// レベルは、0x100 で 100% となる。
	// _b : ストリーム番号
	// _p : チャンネル
	virtual void SetVolume(int _b, int _p, short _vol);
	virtual short GetVolume(int _b, int _p) const;

	// 音源で扱うチャンネル数を返す。
	virtual int GetTrackCount(void) const;

	// 指定したチャンネルの情報へのポインタを返す。
	// 内容については、ssTrackInfo.h 参照。
	virtual ssTrackInfo *GetInfo(int _track);

	// レジスタの内容を返す。
	//   _buffer : レジスタの内容を返すアドレス
	//   _count  : 返すサイズ
	//   _offset : レジスタ先頭からのオフセット
	virtual int GetRegs(BYTE *_buffer, int _count, int _offset = 0);

	// 波形データをレンダリングする。
	//   _buffer : レンダリング先メモリのアドレスの配列(ストリームの数分)
	//   _count  : レンダリングするサンプル数
	virtual void Update(SHORT **_buffer, DWORD _count) = 0;

	// チャンネルマスクを設定する。
	// _mask : マスク(1でマスク,最下位ビットがチャンネル0)
	virtual void SetMask(DWORD _mask);
	virtual DWORD GetMask(void);

protected:
	DWORD m_mask;

private:
	short m_Volume;
};

#endif // __SSSOUNDCHIP_H__
