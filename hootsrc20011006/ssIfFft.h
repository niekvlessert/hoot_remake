#ifndef __SSIFFFT_H__
#define __SSIFFFT_H__

typedef float ssFftReal;
typedef BYTE ssFftLevel;

class ssIfFft
{
public:
	enum {
		CH_L = 0,
		CH_R,
	};

	virtual ~ssIfFft() {};

	virtual void Input(int *_buff, int _count) = 0;
	virtual void Calc(void) = 0;
	virtual ssFftLevel *GetSpectrum(int _ch) const = 0;
	virtual int GetNumSpectrum() const = 0;

	virtual int Enable(int _flag) = 0;
};

#endif // __SSIFFFT_H__
