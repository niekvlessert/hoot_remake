#include "StdAfx.h"
#include "ssFft.h"

#include <math.h>

#ifndef PI
#define PI (3.1415926535897932384626433)
#endif

ssFft::ssFft(int _nSamp, int _nSpec, int _max_freq, int _rate, int _func)
{
	int ch;
	int i;

	m_enable = 1;
	m_nSamp = _nSamp;
	m_nSpec = _nSpec;
	m_nPtr = 0;

	m_sintbl = new ssFftReal[m_nSamp];
	m_costbl = new ssFftReal[m_nSamp];
	m_winfnctype = _func;
	m_winfnc = new ssFftReal[m_nSamp];
	for (i = 0; i < m_nSamp; i++) {
		m_sintbl[i] = sin(2.* PI * i / m_nSamp);
		m_costbl[i] = sin(2.* PI * i / m_nSamp);

		switch (m_winfnctype) {
		case 1:
			// Hanning
			m_winfnc[i] = 0.5 - 0.5 * cos(2*PI*i/m_nSamp);
			break;
		case 2:
			// Hamming
			m_winfnc[i] = 0.54 - 0.46 * cos(2*PI*i/m_nSamp);
			break;
		case 3:
			// Blackman
			m_winfnc[i] = 0.42 - 0.5 * cos(2*PI*i/m_nSamp) + 0.08 * cos(4*PI*i/m_nSamp);
			break;
		default:
			// rectangular
			m_winfnctype = 0;
			break;
		}
	}

	for (ch = 0; ch < 2; ch++) {
		m_wave[ch] = new int[m_nSamp];
		m_spectrum[ch] = new ssFftLevel[m_nSpec];
	}
	m_in = new ssComplex[m_nSamp];
	m_out = new ssFftReal[m_nSamp];
	m_spec_count = new int[m_nSpec];


	m_bitrevtbl = new short[m_nSamp];
	for (i = 0; i < m_nSamp; i++) {
		int x = i;
		int r = 0;
		for (int j = 1; j < m_nSamp; j <<= 1) {
			r = (r<<1) | (x&1);
			x >>= 1;
		}
		m_bitrevtbl[i] = r;
	}

	for (ch = 0; ch < 2; ch++) {
		for (i = 0; i < m_nSamp; i++) {
			m_wave[ch][i] = 0.0;
			m_wave[ch][i] = 0.0;
		}
		for (i = 0; i < m_nSpec; i++) {
			m_spectrum[ch][i] = 0;
		}
	}
	for (i = 0; i < m_nSpec; i++) {
		m_spec_count[i] = 0;
	}

	double max_freq = (double)_max_freq;
	for (i = 1; i < (m_nSamp+1)/2; i++) {
		double freq = (double)_rate * (double)i / (double)m_nSamp;
		int idx = (int)(log(freq/max_freq)/log(1.4) + m_nSpec);
		if (idx >= m_nSpec) {
			break;
		}
		if (idx < 0) idx = 0;
		m_spec_count[idx]++;
	}

	// í‚É0‚É‚È‚é‚Ì‚ð–h‚®
	for (i = 0; i < (m_nSpec - 1); i++) {
		while (m_spec_count[i] <= 0) {
			m_spec_count[i  ]++;
			m_spec_count[i+1]--;
		}
		TRACE("spec_count[%d] = %d\n", i, m_spec_count[i]);
	}
}

ssFft::~ssFft()
{
	int ch;

	delete[] m_sintbl;
	delete[] m_costbl;
	delete[] m_winfnc;

	for (ch = 0; ch < 2; ch++) {
		delete[] m_spectrum[ch];
		delete[] m_wave[ch];
	}
	delete[] m_spec_count;
	delete[] m_in;
	delete[] m_out;


	delete[] m_bitrevtbl;
}

void ssFft::Input(int *_buff, int _count)
{
	int *l, *r;
	int *buff;
	int count;
	const int nSamp = m_nSamp;

	int nPtr = m_nPtr;

	if (_count < nSamp) {
		count = _count;
		buff = _buff;
		l = m_wave[0] + nPtr;
		r = m_wave[1] + nPtr;
		if (count > (m_nSamp - nPtr)) {
			int i;
			for (i = nPtr; i < nSamp; i++) {
				*l++ = *buff++;
				*r++ = *buff++;
			}
			count -= nSamp - nPtr;
			l = m_wave[0];
			r = m_wave[1];
			for (i = 0; i < count; i++) {
				*l++ = *buff++;
				*r++ = *buff++;
			}
			nPtr = count;
		} else {
			for (int i = 0; i < count; i++) {
				*l++ = *buff++;
				*r++ = *buff++;
			}
			nPtr = (nPtr + count) % nSamp;
		}
	} else {
		buff = &_buff[(_count - nSamp) * 2];
		l = m_wave[0];
		r = m_wave[1];

		for (int i = 0; i < nSamp; i++) {
			*l++ = *buff++;
			*r++ = *buff++;
		}

		nPtr = 0;
	}

	m_nPtr = nPtr;
}

void ssFft::CalcFft(int *_wave)
{
	const int *const wave = _wave;
	const int count = m_nSamp;
	ssComplex *const in = m_in;

	int i;

	{
		const ssFftReal *const winfnc = m_winfnc;
		if (m_winfnctype == 0) {
			for (i = 0; i < m_nSamp; i++) {
				in[i].re = (ssFftReal)wave[i];
				in[i].im = 0.0;
			}
		} else {
			for (i = 0; i < m_nSamp; i++) {
				in[i].re = (ssFftReal)wave[i] * winfnc[i];
				in[i].im = 0.0;
			}
		}
	}

	ssFftReal *const sint = m_sintbl;
	ssFftReal *const cost = m_costbl;

	int n_half = count / 2;

	for (int ne = 1; ne < count; ne *= 2) {
		const int n_half2 = n_half*2;
		for (int jp = 0; jp < count; jp = jp + n_half2) {
			int jx = 0;
			for (int j = jp; j < jp + n_half; j++) {
				const int jnh = j + n_half;
				const ssFftReal tre = in[j].re - in[jnh].re;
				const ssFftReal tim = in[j].im - in[jnh].im;
				const ssFftReal s = sint[jx];
				const ssFftReal c = cost[jx];
				in[j].re += in[jnh].re;
				in[j].im += in[jnh].im;
				in[jnh].re =  tre * c + tim * s;
				in[jnh].im = -tre * s + tim * c;
				jx += ne;
			}
		}
		n_half /= 2;
	}

	const short *const tbl = m_bitrevtbl;
	for (i = 0; i < count; i++) {
		const int r = tbl[i];
		if (i < r) {
			const ssComplex tmp = in[i];
			in[i] = in[r];
			in[r] = tmp;
		}
	}

	for (i = 0; i < count; i++) {
		in[i].re /= count;
		in[i].im /= count;
	}

	ssFftReal *out = m_out;
	for (i = 0; i < count; i++) {
		ssFftReal rr = in[i].re;
		ssFftReal ii = in[i].im;
		*out++ = sqrt(rr*rr + ii*ii);
	}
}

void ssFft::Calc(void)
{
	if (!m_enable) {
		memset(m_spectrum[0], 0, m_nSpec);
		memset(m_spectrum[1], 0, m_nSpec);
		return;
	}

	int i;
	int ch;
	ssFftReal *const out = m_out;
	int *const spec_count = m_spec_count;

	for (ch = 0; ch < 2; ch++) {
		CalcFft(m_wave[ch]);
		{
			int idx = 1;
			double d;
			ssFftLevel *const spectrum = m_spectrum[ch];
			for (i = 0; i < m_nSpec; i++) {
				d = 0.0;
				int j;
				for (j = 0; j < spec_count[i]; j++) {
					const ssFftReal d0 = out[idx] + out[m_nSamp - idx];
					if (d0 > d) d = d0;
					idx++;
				}
				d /= 32768*256.;
				int p;
				if (d >= 0.0) {
					p = (int)(16+log10(d)*4.);
					if (p < 0) {
						p = 0;
					} else if (p >= 15) {
						p = 15;
					}
				} else {
					p = 0;
				}
				spectrum[i] = p;
			}
		}
	}
}

ssFftLevel *ssFft::GetSpectrum(int _ch) const
{
	if (_ch == ssIfFft::CH_L) {
		return m_spectrum[0];
	} else if (_ch == ssIfFft::CH_R) {
		return m_spectrum[1];
	}
	return NULL;
}

int ssFft::GetNumSpectrum() const
{
	return m_nSpec;
}

int ssFft::Enable(int _flag)
{
	int prev = m_enable;

	if (_flag != -1) {
		m_enable = _flag;
	}
	return prev;
}
