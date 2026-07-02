#ifndef __MAME_M68000_H__
#define __MAME_M68000_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "m68000/m68000.h"

#define m68ki_remaining_cycles m68k_ICount

static int dummy_intterrupt_callback(int)
{
	return MC68000_INT_ACK_AUTOVECTOR;
}

#if 0
static __inline void m68000_init(void)
{
	m68000_set_irq_callback(dummy_intterrupt_callback);
}
#endif

static __inline int m68000_emulate(int cycles)
{
	return m68000_execute(cycles);
}

static __inline void  m68000_cause_NMI(void)
{
	m68000_set_nmi_line(ASSERT_LINE);
}

static __inline void m68000_raise_IRQ(int _level)
{
	m68000_set_irq_line(_level, ASSERT_LINE);
}

static __inline void m68000_lower_IRQ(int _level)
{
	m68000_set_irq_line(_level, CLEAR_LINE);
}

#ifdef __cplusplus
}
#endif

#endif /* __MAME_M68000_H__ */
