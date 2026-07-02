#ifndef __MAME_CPUINTRF_H__
#define __MAME_CPUINTRF_H__

#include "osd_cpu.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLEAR_LINE		0		/* clear (a fired, held or pulsed) line */
#define ASSERT_LINE		1		/* assert an interrupt immediately */
#define HOLD_LINE		2		/* hold interrupt line until enable is true */
#define PULSE_LINE		3		/* pulse interrupt line for one instruction */

#define MAX_REGS		128 	/* maximum number of register of any CPU */

/* Values passed to the cpu_info function of a core to retrieve information */
enum {
	CPU_INFO_REG,
	CPU_INFO_FLAGS=MAX_REGS,
	CPU_INFO_NAME,
	CPU_INFO_FAMILY,
	CPU_INFO_VERSION,
	CPU_INFO_FILE,
	CPU_INFO_CREDITS,
	CPU_INFO_REG_LAYOUT,
	CPU_INFO_WIN_LAYOUT
};

#define CPU_IS_LE		0	/* emulated CPU is little endian */
#define CPU_IS_BE		1	/* emulated CPU is big endian */

#define REG_PREVIOUSPC	-1
#define REG_SP_CONTENTS -2

/* Memory map constants */
#define CPU_MAP_DIRECT  0  /* Reads/writes are done directly */
#define CPU_MAP_HANDLED 1  /* Reads/writes use a function handler */

#define change_pc(pc)
#define change_pc16(pc)
#define change_pc24bew(pc)
#define change_pc24bedw(pc)
#define change_pc32bew(pc)
#define change_pc32bedw(pc)
unsigned cpu_get_pc(void);

#define cpu_getactivecpu() 0
#define state_save_UINT32(a,b,c,d,e,f)
#define state_save_UINT16(a,b,c,d,e,f)
#define state_save_UINT8(a,b,c,d,e,f)
#define state_load_UINT32(a,b,c,d,e,f)
#define state_load_UINT16(a,b,c,d,e,f)
#define state_load_UINT8(a,b,c,d,e,f)
#define CALL_MAME_DEBUG
static __inline void logerror(char *x,...)
{
}

/* external interface */

#define cpu_init_memmap cpu_init_memmap16
void cpu_init_memmap16(void);
void cpu_init_memmap21(void);
void cpu_init_memmap24(void);

void cpu_map_fetch(offs_t start, offs_t end, UINT8 *memory);
void cpu_map_read(offs_t start, offs_t end, UINT8 *memory);
void cpu_map_write(offs_t start, offs_t end, UINT8 *memory);
void cpu_add_read(offs_t start, offs_t end, int method, void *data);
void cpu_add_read_byte(offs_t start, offs_t end, int method, void *data);
void cpu_add_read_word(offs_t start, offs_t end, int method, void *data);
void cpu_add_read_dword(offs_t start, offs_t end, int method, void *data);
void cpu_add_write(offs_t start, offs_t end, int method, void *data);
void cpu_add_write_byte(offs_t start, offs_t end, int method, void *data);
void cpu_add_write_word(offs_t start, offs_t end, int method, void *data);
void cpu_add_write_dword(offs_t start, offs_t end, int method, void *data);
void cpu_end_memmap(void);

int  cpu_get_context_size(void);
void cpu_set_context(void *context);
void cpu_get_context(void *context);

#ifdef __cplusplus
}
#endif

#endif	/* __MAME_CPUINTRF_H__ */
