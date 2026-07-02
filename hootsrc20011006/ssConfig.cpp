#include "StdAfx.h"
#include "ssConfig.h"

#define CONFIG_SECTION "config"

static list<string> Path2List(string _path)
{
	list<string> ret;

	string::size_type ind = 0;

	_path += ";"; // sentry

	while (1) {
		string::size_type inde = _path.find_first_of(';', ind);
		if (inde == string::npos) {
			break;
		}
		string path = _path.substr(ind, inde - ind) + "\\";
		ret.push_back(path);
		ind = inde + 1;
	}

	return ret;
}

ssConfig::ssConfig()
{
	CWinApp *app = AfxGetApp();
	LPCSTR section = CONFIG_SECTION;

	sampling_rate = app->GetProfileInt(section, "sampling_rate", 44100);
	fft_size = app->GetProfileInt(section, "fft_size", 2048);
	fft_winfnc = app->GetProfileInt(section, "fft_winfnc", 1);
	fft_skip = app->GetProfileInt(section, "fft_skip", 1);
	time_slice = (double)app->GetProfileInt(section, "time_slice", 32) / 1000.0;
	info_buffer = app->GetProfileInt(section, "buffer", 16);
	stream_forward = time_slice * info_buffer;
	stream_buffer_count = sampling_rate * 8;

	lpf = app->GetProfileInt(section, "lpf", 0);
	lpf_order = app->GetProfileInt(section, "lpf_order", 4);
	lpf_cutoff = app->GetProfileInt(section, "lpf_cutoff", 8000);

	ym2203_interpolation = app->GetProfileInt(section, "ym2203_interpolation", 1);
	ym2203_volume_fm = app->GetProfileInt(section, "ym2203_volume_fm", 0);
	ym2203_volume_psg = app->GetProfileInt(section, "ym2203_volume_psg", 0);

	ym2151_interpolation = app->GetProfileInt(section, "ym2151_interpolation", 1);
	ym2151_volume = app->GetProfileInt(section, "ym2151_volume", 0);

	ym2608_interpolation = app->GetProfileInt(section, "ym2608_interpolation", 1);
	ym2608_volume_fm = app->GetProfileInt(section, "ym2608_volume_fm", 0);
	ym2608_volume_psg = app->GetProfileInt(section, "ym2608_volume_psg", 0);
	ym2608_volume_rhythmt = app->GetProfileInt(section, "ym2608_volume_rhythmt", 0);
	ym2608_volume_rhythm0 = app->GetProfileInt(section, "ym2608_volume_rhythm0", 0);
	ym2608_volume_rhythm1 = app->GetProfileInt(section, "ym2608_volume_rhythm1", 0);
	ym2608_volume_rhythm2 = app->GetProfileInt(section, "ym2608_volume_rhythm2", 0);
	ym2608_volume_rhythm3 = app->GetProfileInt(section, "ym2608_volume_rhythm3", 0);
	ym2608_volume_rhythm4 = app->GetProfileInt(section, "ym2608_volume_rhythm4", 0);
	ym2608_volume_rhythm5 = app->GetProfileInt(section, "ym2608_volume_rhythm5", 0);
	ym2608_volume_adpcm = app->GetProfileInt(section, "ym2608_volume_adpcm", 0);

	ym2610_interpolation = app->GetProfileInt(section, "ym2610_interpolation", 1);
	ym2610_volume_fm = app->GetProfileInt(section, "ym2610_volume_fm", 0);
	ym2610_volume_psg = app->GetProfileInt(section, "ym2610_volume_psg", 0);
	ym2610_volume_adpcma = app->GetProfileInt(section, "ym2610_volume_adpcma", 0);
	ym2610_volume_adpcmb = app->GetProfileInt(section, "ym2610_volume_adpcmb", 0);


	CString str;

	str = app->GetProfileString(section, "title_file", "hoot.xml");
	title_file = string((LPCSTR)str);

	str = app->GetProfileString(section, "data_dir", "roms");
	rompath = Path2List(string((LPCSTR)str));

	str = app->GetProfileString(section, "pdx_dir", "roms");
	pdxpath = Path2List(string((LPCSTR)str));

	str = app->GetProfileString(section, "language", "default");
	language = (LPCSTR)str;
}

ssConfig::~ssConfig()
{
}

bool ssConfig::Load(const string &_fname)
{
	return true;
}
