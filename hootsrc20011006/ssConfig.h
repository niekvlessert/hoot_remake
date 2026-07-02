#ifndef __SSCONFIG_H__
#define __SSCONFIG_H__

struct ssConfig
{
	ssConfig();
	~ssConfig();
	bool Load(const string &_fname);

	int sampling_rate;
	int fft_size;
	int fft_winfnc;
	int fft_skip;
	int stream_buffer_count;
	int info_buffer;

	int lpf;
	int lpf_order;
	int lpf_cutoff;

	int ym2203_interpolation;
	int ym2203_volume_fm;
	int ym2203_volume_psg;

	int ym2151_interpolation;
	int ym2151_volume;

	int ym2608_interpolation;
	int ym2608_volume_fm;
	int ym2608_volume_psg;
	int ym2608_volume_rhythmt;
	int ym2608_volume_rhythm0;
	int ym2608_volume_rhythm1;
	int ym2608_volume_rhythm2;
	int ym2608_volume_rhythm3;
	int ym2608_volume_rhythm4;
	int ym2608_volume_rhythm5;
	int ym2608_volume_adpcm;

	int ym2610_interpolation;
	int ym2610_volume_fm;
	int ym2610_volume_psg;
	int ym2610_volume_adpcma;
	int ym2610_volume_adpcmb;

	double stream_forward;
	double time_slice;

	string title_file;
	list<string> rompath;

	list<string> pdxpath;

	string language;
};

#endif // __SSCONFIG_H__
