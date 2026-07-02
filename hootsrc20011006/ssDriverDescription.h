#ifndef __SSDRIVERDESCRIPTION_H__
#define __SSDRIVERDESCRIPTION_H__

struct ssDriverDescription
{
	enum {
		MAX_DRIVER_DESCRIPTION = 8,
		MAX_FILE_DESCRIPTION = 8,
		MAX_OPTION_DESCRIPTION = 8,
		MAX_FILES = 8,
		MAX_OPTIONS = 16,
	};

	struct Files
	{
		const char *name;
		const char *descriptions[MAX_FILE_DESCRIPTION];
	};
	struct Options
	{
		const char *name;
		int value;
		const char *descriptions[MAX_OPTION_DESCRIPTION];
	};

	const char *name;
	const char *type;
	const char *descriptions[MAX_DRIVER_DESCRIPTION];
	Files files[MAX_FILES];
	Options options[MAX_OPTIONS];
};

#endif // __SSDRIVERDESCRIPTION_H__
