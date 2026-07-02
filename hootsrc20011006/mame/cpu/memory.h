#ifndef __MAME_MEMORY_H
#define __MAME_MEMORY_H

#include "osd_cpu.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNUSEDARG

typedef UINT8			data8_t;
typedef UINT16			data16_t;
typedef UINT32			data32_t;
typedef UINT32			offs_t;

typedef UINT32			data_t;

typedef data8_t			(*read8_handler)  (UNUSEDARG offs_t offset);
typedef void			(*write8_handler) (UNUSEDARG offs_t offset, UNUSEDARG data8_t data);
typedef data16_t		(*read16_handler) (UNUSEDARG offs_t offset, UNUSEDARG data16_t mem_mask);
typedef void			(*write16_handler)(UNUSEDARG offs_t offset, UNUSEDARG data16_t data, UNUSEDARG data16_t mem_mask);
typedef data32_t		(*read32_handler) (UNUSEDARG offs_t offset, UNUSEDARG data32_t mem_mask);
typedef void			(*write32_handler)(UNUSEDARG offs_t offset, UNUSEDARG data32_t data, UNUSEDARG data32_t mem_mask);
typedef offs_t			(*opbase_handler) (UNUSEDARG offs_t address);

typedef read8_handler	mem_read_handler;
typedef write8_handler	mem_write_handler;
typedef read16_handler	mem_read16_handler;
typedef write16_handler	mem_write16_handler;
typedef read32_handler	mem_read32_handler;
typedef write32_handler	mem_write32_handler;

typedef read8_handler	port_read_handler;
typedef write8_handler	port_write_handler;
typedef read16_handler	port_read16_handler;
typedef write16_handler	port_write16_handler;
typedef read32_handler	port_read32_handler;
typedef write32_handler	port_write32_handler;

extern offs_t encrypted_opcode_start[],encrypted_opcode_end[];

#define READ_HANDLER(name) 		data8_t  name(UNUSEDARG offs_t offset)
#define WRITE_HANDLER(name) 	void     name(UNUSEDARG offs_t offset, UNUSEDARG data8_t data)
#define READ16_HANDLER(name)	data16_t name(UNUSEDARG offs_t offset, UNUSEDARG data16_t mem_mask)
#define WRITE16_HANDLER(name)	void     name(UNUSEDARG offs_t offset, UNUSEDARG data16_t data, UNUSEDARG data16_t mem_mask)
#define READ32_HANDLER(name)	data32_t name(UNUSEDARG offs_t offset, UNUSEDARG data32_t mem_mask)
#define WRITE32_HANDLER(name)	void     name(UNUSEDARG offs_t offset, UNUSEDARG data32_t data, UNUSEDARG data32_t mem_mask)
#define OPBASE_HANDLER(name)	offs_t   name(UNUSEDARG offs_t address)

#define BYTE_XOR_BE(a)  	((a) ^ 1)
#define BYTE_XOR_LE(a)  	(a)
#define BYTE4_XOR_BE(a) 	((a) ^ 3)
#define BYTE4_XOR_LE(a) 	(a)
#define WORD_XOR_BE(a)  	((a) ^ 2)
#define WORD_XOR_LE(a)  	(a)

#define COMBINE_DATA(varptr)		(*(varptr) = (*(varptr) & mem_mask) | (data & ~mem_mask))

#define ACCESSING_LSB16				((mem_mask & 0x00ff) == 0)
#define ACCESSING_MSB16				((mem_mask & 0xff00) == 0)
#define ACCESSING_LSB				ACCESSING_LSB16
#define ACCESSING_MSB				ACCESSING_MSB16

#define ACCESSING_LSW32				((mem_mask & 0x0000ffff) == 0)
#define ACCESSING_MSW32				((mem_mask & 0xffff0000) == 0)
#define ACCESSING_LSB32				((mem_mask & 0x000000ff) == 0)
#define ACCESSING_MSB32				((mem_mask & 0xff000000) == 0)

#define cpu_readmem16			cpu_readmem
#define cpu_writemem16			cpu_writemem
#define cpu_readmem21			cpu_readmem
#define cpu_writemem21			cpu_writemem

#define cpu_readmem24bew		cpu_readmem
#define cpu_writemem24bew		cpu_writemem
#define cpu_readmem24bew_word	cpu_readmem_word
#define cpu_writemem24bew_word	cpu_writemem_word

#define cpu_readmem24bedw		cpu_readmem
#define cpu_writemem24bedw		cpu_writemem
#define cpu_readmem24bedw_word	cpu_readmem_word
#define cpu_writemem24bedw_word	cpu_writemem_word
#define cpu_readmem24bedw_dword	cpu_readmem_dword
#define cpu_writemem24bedw_dword	cpu_writemem_dword

#define cpu_readmem32bedw		cpu_readmem
#define cpu_writemem32bedw		cpu_writemem
#define cpu_readmem32bedw_word	cpu_readmem_word
#define cpu_writemem32bedw_word	cpu_writemem_word
#define cpu_readmem32bedw_dword	cpu_readmem_dword
#define cpu_writemem32bedw_dword	cpu_writemem_dword

data8_t cpu_readop(offs_t offset);
data8_t cpu_readop_arg(offs_t offset);
data16_t cpu_readop16(offs_t offset);
data16_t cpu_readop_arg16(offs_t offset);

data8_t cpu_readmem(offs_t offset);
void cpu_writemem(offs_t offset, data8_t data);
data16_t cpu_readmem_word(offs_t offset);
void cpu_writemem_word(offs_t offset, data16_t data);
data32_t cpu_readmem_dword(offs_t offset);
void cpu_writemem_dword(offs_t offset, data32_t data);

data8_t cpu_readport16(offs_t offset);
void cpu_writeport16(offs_t offset, data8_t data);

#ifdef __cplusplus
}
#endif

#endif /* __MAME_MEMORY_H__ */
