#ifndef __LIB_LOG2_H
#define __LIB_LOG2_H

static inline int
fls (int x)
{
	int r;
	__asm__ ("bsrl %1,%0\n\t"
					 "jnz 1f\n\t"
					 "movl $-1,%0\n"
					 "1:" : "=r" (r) : "rm" (x));
	return r + 1;
}

static inline unsigned int
roundup_pow_of_two (unsigned int x)
{
	return 1UL << fls (x - 1);
}

#endif
