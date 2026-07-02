#ifndef __MAME_DRIVER_H__
#define __MAME_DRIVER_H__

// MAMEのソースをだますためのヘッダ
#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>

#include "../cpu/memory.h"

#pragma warning (disable:4101)
#pragma warning (disable:4244)
#pragma warning (disable:4761)
#pragma warning (disable:4018)
#pragma warning (disable:4146)
#pragma warning (disable:4068)
#pragma warning (disable:4005)
#pragma warning (disable:4305)

#define INLINE static __inline

#ifndef OSD_CPU_H
#define OSD_CPU_H
typedef unsigned char	UINT8;   /* unsigned  8bit */
typedef unsigned short	UINT16;  /* unsigned 16bit */
typedef unsigned int	UINT32;  /* unsigned 32bit */
typedef signed char		INT8;    /* signed  8bit   */
typedef signed short	INT16;   /* signed 16bit   */
typedef signed int		INT32;   /* signed 32bit   */
#endif

#ifdef _FILE_DEFINED
static FILE *errorlog = NULL;
#define CLIB_DECL
static __inline void logerror(char *x,...)
{
}
#endif

typedef UINT32 offs_t;
typedef UINT32 data_t;

#if 0
typedef data_t (*mem_read_handler)(offs_t offset);
typedef void (*mem_write_handler)(offs_t offset,data_t data);
typedef offs_t (*opbase_handler)(offs_t address);
#endif

// for fm.c
#define HAS_YM2203 1
#define HAS_YM2608 1
#define HAS_YM2610 1
#define HAS_YM2610B 0
#define HAS_YM2612 0
#define HAS_YM2151 1

// for fmopl.c
#define HAS_YM3812 1
#define HAS_YM3526 0
#define HAS_Y8950 0

#undef STEREO_MIX

#define YM2203UpdateRequest(chip) ((void *)0)
#define YM2608UpdateRequest(chip) ((void *)0)
#define YM2610UpdateRequest(chip) ((void *)0)
#define YM2612UpdateRequest(chip) ((void *)0)
#define YM2151UpdateRequest(chip) ((void *)0)


// for ym2151.c
typedef void (*YM2151_TIMERHANDLER)(int n,int c,int cnt,double stepTime);

// for es5506.c
#include "es5506.h"

struct MachineSound
{
	int sound_type;
	void *sound_interface;
};

struct RunningMachine
{
	int sample_rate;
};

extern struct RunningMachine *Machine;

void *memory_region(int _region);
void set_memory_region(int _region, void *_addr);

#define sound_name(a) "DUMMY"
#define stream_init_multi(a,b,c,d,e,f) (0)
#define stream_update(a,b)
#define cpu_getpreviouspc() (0)

#ifdef __cplusplus
};
#endif

#endif // __MAME_DRIVER_H__
