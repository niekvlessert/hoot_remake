#ifndef __MAME_M6809_H__
#define __MAME_M6809_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
  USE_XM7_6809 が定義されていると XM7 の 6809 コアを使用。
  そうでなければ、MAME の 6809 コアを使用。
*/
//#define USE_XM7_6809
#ifdef USE_XM7_6809

/* XM7 */

#include "../xm7/x6809.h"

static __inline void m6809_init(void)
{
	x6809_init();
}

static __inline void m6809_reset(void *param)
{
	x6809_reset(param);
}

static __inline unsigned m6809_get_pc(void)
{
	return x6809_get_pc();
}

static __inline int m6809_emulate(int cycles)
{
	return x6809_execute(cycles);
}

static __inline void  m6809_cause_NMI(void)
{
	x6809_cause_NMI();
}

static __inline void m6809_raise_IRQ(void)
{
	x6809_raise_IRQ();
}

static __inline void m6809_lower_IRQ(void)
{
	x6809_lower_IRQ();
}

static __inline void m6809_raise_FIRQ(void)
{
	x6809_raise_FIRQ();
}

static __inline void m6809_lower_FIRQ(void)
{
	x6809_lower_FIRQ();
}

#else /* USE_XM7_6809 */

/* MAME */

#include "m6809/m6809.h"

static int m6809_irq_status[8];

static int m6809_intterrupt_callback(int i)
{
	if (m6809_irq_status[i] == HOLD_LINE) {
		m6809_irq_status[i] = CLEAR_LINE;
		m6809_set_irq_line(i, CLEAR_LINE);
	}
	return 0;
}

static __inline void m6809_init(void)
{
	int i;
	for (i = 0; i < 8; i++) {
		m6809_irq_status[i] = CLEAR_LINE;
	}
	m6809_set_irq_callback(m6809_intterrupt_callback);
}

static __inline int m6809_emulate(int cycles)
{
	return m6809_execute(cycles);
}

static __inline void  m6809_cause_NMI(void)
{
	m6809_set_nmi_line(ASSERT_LINE);
}

static __inline void m6809_hold_IRQ(void)
{
	m6809_irq_status[M6809_IRQ_LINE] = HOLD_LINE;
	m6809_set_irq_line(M6809_IRQ_LINE, HOLD_LINE);
}

static __inline void m6809_raise_IRQ(void)
{
	m6809_irq_status[M6809_IRQ_LINE] = ASSERT_LINE;
	m6809_set_irq_line(M6809_IRQ_LINE, ASSERT_LINE);
}

static __inline void m6809_lower_IRQ(void)
{
	m6809_irq_status[M6809_IRQ_LINE] = CLEAR_LINE;
	m6809_set_irq_line(M6809_IRQ_LINE, CLEAR_LINE);
}

static __inline void m6809_hold_FIRQ(void)
{
	m6809_irq_status[M6809_FIRQ_LINE] = HOLD_LINE;
	m6809_set_irq_line(M6809_FIRQ_LINE, HOLD_LINE);
}

static __inline void m6809_raise_FIRQ(void)
{
	m6809_irq_status[M6809_FIRQ_LINE] = ASSERT_LINE;
	m6809_set_irq_line(M6809_FIRQ_LINE, ASSERT_LINE);
}

static __inline void m6809_lower_FIRQ(void)
{
	m6809_irq_status[M6809_FIRQ_LINE] = CLEAR_LINE;
	m6809_set_irq_line(M6809_FIRQ_LINE, CLEAR_LINE);
}

#endif /* USE_XM7_6809 */

#ifdef __cplusplus
}
#endif

#endif /* __MAME_M6809_H__ */
