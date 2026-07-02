#ifndef __SSFFT_H__
#define __SSFFT_H__

#include "ssIfFft.h"

class ssFft : public ssIfFft
{
public:
	// Window Function
	// 0 : rectangular
	// 1 : Hanning
	// 2 : Hamming
	// 3 : Blackman
	ssFft(int _nSamp, int _nSpec, int _max_freq, int _rate, int _func = 0);
	~ssFft();

	void Input(int *_buff, int _count);
	void Calc(void);
	ssFftLevel *GetSpectrum(int _ch) const;
	int GetNumSpectrum() const;

	int Enable(int _flag);

private:
	void CalcFft(int *_wave);

private:
	struct ssComplex
	{
		ssFftReal re;
		ssFftReal im;
	};

	int m_nSamp;
	int m_nSpec;
	int m_buff_count;
	int m_enable;
	int m_nPtr;

	ssFftReal *m_sintbl;
	ssFftReal *m_costbl;

	int m_winfnctype;
	ssFftReal *m_winfnc;

	int *m_wave[2];
	ssComplex *m_in;
	ssFftReal *m_out;

	int *m_spec_count;
	ssFftLevel *m_spectrum[2];


	short *m_bitrevtbl;
};

#endif // __SSFFT_H__
