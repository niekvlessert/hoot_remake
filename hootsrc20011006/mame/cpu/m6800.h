#ifndef __MAME_M6800_H__
#define __MAME_M6800_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "m6800/m6800.h"

static int dummy_intterrupt_callback(int)
{
	return 0;
}

static __inline void hd63701_init_(void)
{
	hd63701_init();
	hd63701_set_irq_callback(dummy_intterrupt_callback);
}
#define hd63701_init hd63701_init_

static __inline int hd63701_emulate(int cycles)
{
	return hd63701_execute(cycles);
}

static __inline void  hd63701_cause_NMI(void)
{
	hd63701_set_nmi_line(ASSERT_LINE);
}

static __inline void hd63701_raise_IRQ(void)
{
	hd63701_set_irq_line(HD63701_IRQ_LINE, ASSERT_LINE);
}

static __inline void hd63701_lower_IRQ(void)
{
	hd63701_set_irq_line(HD63701_IRQ_LINE, CLEAR_LINE);
}

static __inline void hd63701_raise_TIN(void)
{
	hd63701_set_irq_line(HD63701_TIN_LINE, ASSERT_LINE);
}

static __inline void hd63701_lower_TIN(void)
{
	hd63701_set_irq_line(HD63701_TIN_LINE, CLEAR_LINE);
}

#ifdef __cplusplus
}
#endif

#endif /* __MAME_M6800_H__ */
