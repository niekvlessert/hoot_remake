#ifndef __MAME_H6280_H__
#define __MAME_H6280_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "h6280/h6280.h"

static int dummy_intterrupt_callback(int)
{
	return 0;
}

static __inline void h6280_init_(void)
{
	h6280_init();
	h6280_set_irq_callback(dummy_intterrupt_callback);
}
#define h6280_init h6280_init_

static __inline int h6280_emulate(int cycles)
{
	return h6280_execute(cycles);
}

static __inline void h6280_skip_idle(void)
{
	h6280_ICount = 0;
}

static __inline void h6280_raise_NMI(void)
{
	h6280_set_nmi_line(ASSERT_LINE);
}

static __inline void h6280_lower_NMI(void)
{
	h6280_set_nmi_line(CLEAR_LINE);
}

static __inline void h6280_raise_IRQ1(void)
{
	h6280_set_irq_line(0, ASSERT_LINE);
}

static __inline void h6280_lower_IRQ1(void)
{
	h6280_set_irq_line(0, CLEAR_LINE);
}

static __inline void h6280_raise_IRQ2(void)
{
	h6280_set_irq_line(1, ASSERT_LINE);
}

static __inline void h6280_lower_IRQ2(void)
{
	h6280_set_irq_line(1, CLEAR_LINE);
}

static __inline void h6280_raise_TIMER(void)
{
	h6280_set_irq_line(2, ASSERT_LINE);
}

static __inline void h6280_lower_TIMER(void)
{
	h6280_set_irq_line(2, CLEAR_LINE);
}

#ifdef __cplusplus
}
#endif

#endif /* __MAME_H6280_H__ */
