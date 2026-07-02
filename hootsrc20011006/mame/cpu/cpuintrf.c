#include "cpuintrf.h"
#include "memory.h"
#include <string.h>

/*
  制限:
  ・エラーチェックはいい加減。
  ・バンクをまたいだワードアクセスはできない。
*/

typedef data8_t (*read_func_t)(offs_t offset);
typedef void (*write_func_t)(offs_t offset,  data8_t data);
typedef data16_t (*read16_func_t)(offs_t offset);
typedef void (*write16_func_t)(offs_t offset,  data16_t data);
typedef data32_t (*read32_func_t)(offs_t offset);
typedef void (*write32_func_t)(offs_t offset,  data32_t data);

struct handler_t
{
	offs_t start;
	offs_t end;
	int method;
	void *data;
};

#define MAX_HANDLER 64

static struct {
	int mem_shift;
	int mem_mask;
	/* マップかハンドラか */
	read_func_t read_func[256];
	write_func_t write_func[256];
	read16_func_t read_func_word[256];
	write16_func_t write_func_word[256];
	read32_func_t read_func_dword[256];
	write32_func_t write_func_dword[256];
	/* ハンドラ */
	struct handler_t read_handler[MAX_HANDLER];
	struct handler_t write_handler[MAX_HANDLER];
	struct handler_t read_handler_word[MAX_HANDLER];
	struct handler_t write_handler_word[MAX_HANDLER];
	struct handler_t read_handler_dword[MAX_HANDLER];
	struct handler_t write_handler_dword[MAX_HANDLER];

	UINT8 *fetch[256];
	UINT8 *read[256];
	UINT8 *write[256];
	UINT8 port[256];
} cpu;

static UINT8 dummy[0x10000];

offs_t encrypted_opcode_start[1],encrypted_opcode_end[1];

/* static functions */

/* ハンドラのないバンク(BYTE) */
static data8_t readmem_mapped(offs_t offset)
{
	return (data8_t)cpu.read[(offset>>cpu.mem_shift)&0xff][offset&cpu.mem_mask];
}

/* ハンドラがあるバンク(BYTE) */
static data8_t readmem_handler(offs_t offset)
{
	int i = 0;
	while (cpu.read_handler[i].data != NULL) {
		struct handler_t *h = &cpu.read_handler[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					return ((UINT8 *)h->data)[offset - h->start];
				} else {
					return ((read_func_t)h->data)(offset);
				}
			}
		} else {
			break;
		}
		i++;
	}
	return readmem_mapped(offset);
}

/* ハンドラのないバンク(BYTE) */
static void writemem_mapped(offs_t offset, data8_t data)
{
	cpu.write[(offset>>cpu.mem_shift)&0xff][offset&cpu.mem_mask] = data;
}

/* ハンドラがあるバンク(BYTE) */
static void writemem_handler(offs_t offset, data8_t data)
{
	int i = 0;
	while (cpu.write_handler[i].data != NULL) {
		struct handler_t *h = &cpu.write_handler[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					((UINT8 *)h->data)[offset - h->start] = data;
				} else {
					((write_func_t)h->data)(offset, data);
				}
				return;
			}
		} else {
			break;
		}
		i++;
	}
	writemem_mapped(offset, data);
}

/* ハンドラのないバンク(WORD) */
static data16_t readmem_mapped_word(offs_t offset)
{
	UINT8 hh = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask)    ];
	UINT8 ll = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 1];
	return (data16_t)((hh<<8) + ll);
}

/* ハンドラがあるバンク(WORD) */
static data16_t readmem_handler_word(offs_t offset)
{
	int i = 0;
	while (cpu.read_handler_word[i].data != NULL) {
		struct handler_t *h = &cpu.read_handler_word[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					UINT8 hh = ((UINT8 *)h->data)[offset - h->start    ];
					UINT8 ll = ((UINT8 *)h->data)[offset - h->start + 1];
					return (data16_t)((hh<<8) + ll);
				} else {
					return ((read_func_t)h->data)(offset);
				}
			}
		} else {
			break;
		}
		i++;
	}
	return readmem_mapped_word(offset);
}

/* ハンドラのないバンク(WORD) */
static void writemem_mapped_word(offs_t offset, data16_t data)
{
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask)    ] = data >> 8;
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 1] = data;
}

/* ハンドラがあるバンク(WORD) */
static void writemem_handler_word(offs_t offset, data16_t data)
{
	int i = 0;
	while (cpu.write_handler_word[i].data != NULL) {
		struct handler_t *h = &cpu.write_handler_word[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					((UINT8 *)h->data)[offset - h->start    ] = data >> 8;
					((UINT8 *)h->data)[offset - h->start + 1] = data;
				} else {
					((write_func_t)h->data)(offset, data);
				}
				return;
			}
		} else {
			break;
		}
		i++;
	}
	writemem_mapped_word(offset, data);
}

/* ハンドラのないバンク(DWORD) */
static data32_t readmem_mapped_dword(offs_t offset)
{
	UINT8 whh = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask)    ];
	UINT8 whl = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 1];
	UINT8 wlh = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 2];
	UINT8 wll = cpu.read[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 3];
	return (data32_t)((whh<<24) + (whl<<16) + (wlh<<8) + wll);
}

/* ハンドラがあるバンク(DWORD) */
static data32_t readmem_handler_dword(offs_t offset)
{
	int i = 0;
	while (cpu.read_handler_dword[i].data != NULL) {
		struct handler_t *h = &cpu.read_handler_dword[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					UINT8 whh = ((UINT8 *)h->data)[offset - h->start    ];
					UINT8 whl = ((UINT8 *)h->data)[offset - h->start + 1];
					UINT8 wlh = ((UINT8 *)h->data)[offset - h->start + 2];
					UINT8 wll = ((UINT8 *)h->data)[offset - h->start + 3];
					return (data32_t)((whh<<24) + (whl<<16) + (wlh<<8) + wll);
				} else {
					return ((read_func_t)h->data)(offset);
				}
			}
		} else {
			break;
		}
		i++;
	}
	return readmem_mapped_dword(offset);
}

/* ハンドラのないバンク(DWORD) */
static void writemem_mapped_dword(offs_t offset, data32_t data)
{
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask)    ] = data >> 24;
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 1] = data >> 16;
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 2] = data >> 8;
	cpu.write[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 3] = data;
}

/* ハンドラがあるバンク(DWORD) */
static void writemem_handler_dword(offs_t offset, data32_t data)
{
	int i = 0;
	while (cpu.write_handler_word[i].data != NULL) {
		struct handler_t *h = &cpu.write_handler_word[i];
		if (offset >= h->start) {
			if (offset <= h->end) {
				if (h->method == CPU_MAP_DIRECT) {
					((UINT8 *)h->data)[offset - h->start    ] = data >> 24;
					((UINT8 *)h->data)[offset - h->start + 1] = data >> 16;
					((UINT8 *)h->data)[offset - h->start + 2] = data >> 8;
					((UINT8 *)h->data)[offset - h->start + 3] = data;
				} else {
					((write_func_t)h->data)(offset, data);
				}
				return;
			}
		} else {
			break;
		}
		i++;
	}
	writemem_mapped_dword(offset, data);
}


/* external interface */

/* 命令フェッチ */
data8_t cpu_readop(offs_t offset)
{
	return (data8_t)cpu.fetch[(offset>>cpu.mem_shift)&0xff][offset&cpu.mem_mask];
}

/* 命令引数フェッチ */
data8_t cpu_readop_arg(offs_t offset)
{
	return cpu_readop(offset);
}

/* 命令フェッチ(WORD) */
data16_t cpu_readop16(offs_t offset)
{
	UINT8 hh = cpu.fetch[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask)    ];
	UINT8 ll = cpu.fetch[(offset>>cpu.mem_shift)&0xff][(offset&cpu.mem_mask) + 1];
	return (data16_t)((hh<<8) + ll);
}

/* 命令引数フェッチ(WORD) */
data16_t cpu_readop_arg16(offs_t offset)
{
	return cpu_readop16(offset);
}

/* メモリ読み込み(BYTE) */
data8_t cpu_readmem(offs_t offset)
{
	return (cpu.read_func[(offset>>cpu.mem_shift)&0xff])(offset);
}

/* メモリ書き込み(BYTE) */
void cpu_writemem(offs_t offset, data8_t data)
{
	(cpu.write_func[(offset>>cpu.mem_shift)&0xff])(offset, data);
}

/* メモリ読み込み(WORD) */
data16_t cpu_readmem_word(offs_t offset)
{
	return (cpu.read_func_word[(offset>>cpu.mem_shift)&0xff])(offset);
}

/* メモリ書き込み(WORD) */
void cpu_writemem_word(offs_t offset, data16_t data)
{
	(cpu.write_func_word[(offset>>cpu.mem_shift)&0xff])(offset, data);
}

/* メモリ読み込み(DWORD) */
data32_t cpu_readmem_dword(offs_t offset)
{
	return (cpu.read_func_dword[(offset>>cpu.mem_shift)&0xff])(offset);
}

/* メモリ書き込み(DWORD) */
void cpu_writemem_dword(offs_t offset, data32_t data)
{
	(cpu.write_func_dword[(offset>>cpu.mem_shift)&0xff])(offset, data);
}

/* I/O ポート */
data8_t cpu_readport16(offs_t offset)
{
	return cpu.port[offset];
}

void cpu_writeport16(offs_t offset, data8_t data)
{
	cpu.port[offset] = data;
}


/* メモリハンドラ登録関連 */

static void init_memmap(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		cpu.read_func[i] = readmem_mapped;
		cpu.write_func[i] = writemem_mapped;
		cpu.read_func_word[i] = readmem_mapped_word;
		cpu.write_func_word[i] = writemem_mapped_word;
		cpu.read_func_dword[i] = readmem_mapped_dword;
		cpu.write_func_dword[i] = writemem_mapped_dword;
	}
	for (i = 0; i < 256; i++) {
		cpu.fetch[i] = dummy;
		cpu.read[i] = dummy;
		cpu.write[i] = dummy;
	}
	for (i = 0; i < MAX_HANDLER; i++) {
		cpu.read_handler[i].data = NULL;
		cpu.write_handler[i].data = NULL;
		cpu.read_handler_word[i].data = NULL;
		cpu.write_handler_word[i].data = NULL;
		cpu.read_handler_dword[i].data = NULL;
		cpu.write_handler_dword[i].data = NULL;
	}
	memset(cpu.port, 0xff, sizeof(cpu.port));

	memset(dummy, 0, sizeof(dummy));
}

#define INIT_MEMMAP(WIDTH, SHIFT, MASK)		\
void cpu_init_memmap##WIDTH##(void)			\
{											\
	cpu.mem_shift = SHIFT;					\
	cpu.mem_mask = MASK;					\
	init_memmap();							\
	encrypted_opcode_start[0] = 1 << WIDTH;	\
	encrypted_opcode_end[0] = 0;			\
}

INIT_MEMMAP(16,  8, 0x00ff)
INIT_MEMMAP(24, 16, 0xffff)
INIT_MEMMAP(21, 13, 0x1fff)

void cpu_map_fetch(offs_t start, offs_t end, UINT8 *memory)
{
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	int i;
	for (i = s; i <= e; i++) {
		cpu.fetch[i] = memory + (i-s)*(1<<cpu.mem_shift);
	}
}

void cpu_map_read(offs_t start, offs_t end, UINT8 *memory)
{
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	int i;
	for (i = s; i <= e; i++) {
		cpu.read[i] = memory + (i-s)*(1<<cpu.mem_shift);
	}
}

void cpu_map_write(offs_t start, offs_t end, UINT8 *memory)
{
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	int i;
	for (i = s; i <= e; i++) {
		cpu.write[i] = memory + (i-s)*(1<<cpu.mem_shift);
	}
}

void cpu_add_read(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.read_func[i] = readmem_handler;
		cpu.read_func_word[i] = readmem_handler_word;
		cpu.read_func_dword[i] = readmem_handler_dword;
	}
	for (i = 0; cpu.read_handler[i].data != NULL; i++);
	cpu.read_handler[i].start = start;
	cpu.read_handler[i].end = end;
	cpu.read_handler[i].method = method;
	cpu.read_handler[i].data = data;

	for (i = 0; cpu.read_handler_word[i].data != NULL; i++);
	cpu.read_handler_word[i].start = start;
	cpu.read_handler_word[i].end = end;
	cpu.read_handler_word[i].method = method;
	cpu.read_handler_word[i].data = data;

	for (i = 0; cpu.read_handler_dword[i].data != NULL; i++);
	cpu.read_handler_dword[i].start = start;
	cpu.read_handler_dword[i].end = end;
	cpu.read_handler_dword[i].method = method;
	cpu.read_handler_dword[i].data = data;
}

void cpu_add_read_byte(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.read_func[i] = readmem_handler;
	}
	for (i = 0; cpu.read_handler[i].data != NULL; i++);
	cpu.read_handler[i].start = start;
	cpu.read_handler[i].end = end;
	cpu.read_handler[i].method = method;
	cpu.read_handler[i].data = data;
}

void cpu_add_read_word(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.read_func_word[i] = readmem_handler_word;
	}
	for (i = 0; cpu.read_handler_word[i].data != NULL; i++);
	cpu.read_handler_word[i].start = start;
	cpu.read_handler_word[i].end = end;
	cpu.read_handler_word[i].method = method;
	cpu.read_handler_word[i].data = data;
}

void cpu_add_read_dword(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.read_func_dword[i] = readmem_handler_dword;
	}
	for (i = 0; cpu.read_handler_dword[i].data != NULL; i++);
	cpu.read_handler_dword[i].start = start;
	cpu.read_handler_dword[i].end = end;
	cpu.read_handler_dword[i].method = method;
	cpu.read_handler_dword[i].data = data;
}

void cpu_add_write(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.write_func[i] = writemem_handler;
		cpu.write_func_word[i] = writemem_handler_word;
		cpu.write_func_dword[i] = writemem_handler_dword;
	}
	for (i = 0; cpu.write_handler[i].data != NULL; i++);
	cpu.write_handler[i].start = start;
	cpu.write_handler[i].end = end;
	cpu.write_handler[i].method = method;
	cpu.write_handler[i].data = data;

	for (i = 0; cpu.write_handler_word[i].data != NULL; i++);
	cpu.write_handler_word[i].start = start;
	cpu.write_handler_word[i].end = end;
	cpu.write_handler_word[i].method = method;
	cpu.write_handler_word[i].data = data;

	for (i = 0; cpu.write_handler_dword[i].data != NULL; i++);
	cpu.write_handler_dword[i].start = start;
	cpu.write_handler_dword[i].end = end;
	cpu.write_handler_dword[i].method = method;
	cpu.write_handler_dword[i].data = data;
}

void cpu_add_write_byte(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.write_func[i] = writemem_handler;
	}
	for (i = 0; cpu.write_handler[i].data != NULL; i++);
	cpu.write_handler[i].start = start;
	cpu.write_handler[i].end = end;
	cpu.write_handler[i].method = method;
	cpu.write_handler[i].data = data;
}

void cpu_add_write_word(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.write_func_word[i] = writemem_handler_word;
	}
	for (i = 0; cpu.write_handler_word[i].data != NULL; i++);
	cpu.write_handler_word[i].start = start;
	cpu.write_handler_word[i].end = end;
	cpu.write_handler_word[i].method = method;
	cpu.write_handler_word[i].data = data;
}

void cpu_add_write_dword(offs_t start, offs_t end, int method, void *data)
{
	int i;
	int s = start >> cpu.mem_shift;
	int e = end >> cpu.mem_shift;
	for (i = s; i <= e; i++) {
		cpu.write_func_dword[i] = writemem_handler_dword;
	}
	for (i = 0; cpu.write_handler_dword[i].data != NULL; i++);
	cpu.write_handler_dword[i].start = start;
	cpu.write_handler_dword[i].end = end;
	cpu.write_handler_dword[i].method = method;
	cpu.write_handler_dword[i].data = data;
}

static void sort_handler(struct handler_t *h)
{
	struct handler_t tmp;
	int s;
	int i;

	for (s = 0; h[s+1].data != NULL; s++) {
		for (i = s; h[i+1].data != NULL; i++) {
			if (h[i].start > h[i+1].start) {
				tmp = h[i];
				h[i] = h[i+1];
				h[i+1] = tmp;
			}
		}
		h++;
	}
}

void cpu_end_memmap(void)
{
	sort_handler(cpu.read_handler);
	sort_handler(cpu.write_handler);

	sort_handler(cpu.read_handler_word);
	sort_handler(cpu.write_handler_word);

	sort_handler(cpu.read_handler_dword);
	sort_handler(cpu.write_handler_dword);
}

int cpu_get_context_size(void)
{
	return sizeof(cpu);
}

void cpu_set_context(void *context)
{
	memcpy(context, &cpu, sizeof(cpu));
}

void cpu_get_context(void *context)
{
	memcpy(&cpu, context, sizeof(cpu));
}

/* dummy */
unsigned cpu_get_pc() {
	return 0;
}
