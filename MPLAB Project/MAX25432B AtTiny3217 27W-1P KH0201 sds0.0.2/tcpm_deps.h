#ifndef __TCPM_DEPS_H
#define __TCPM_DEPS_H

#define BIT(nr) (1UL << (nr))

#include <stdint.h>
#include "typecheck.h"

#include <stddef.h> /* needed for offsetof macro below */

void print(char *format, ...);

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define time_after(a,b)		\
	(typecheck(unsigned long, a) && \
	 typecheck(unsigned long, b) && \
	 ((long)((b) - (a)) < 0))
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)	\
	(typecheck(unsigned long, a) && \
	 typecheck(unsigned long, b) && \
	 ((long)((a) - (b)) >= 0))
#define time_before_eq(a,b)	time_after_eq(b,a)

#define time_is_after_jiffies(a) time_before(get_ms_clk(), a)

#endif
