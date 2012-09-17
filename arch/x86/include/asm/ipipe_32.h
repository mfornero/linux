/*   -*- linux-c -*-
 *   arch/x86/include/asm/ipipe_32.h
 *
 *   Copyright (C) 2002-2012 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __X86_IPIPE_32_H
#define __X86_IPIPE_32_H

#define ipipe_read_tsc(t)  __asm__ __volatile__("rdtsc" : "=A" (t))

#define ipipe_tsc2ns(t) \
({ \
	unsigned long long delta = (t)*1000; \
	do_div(delta, cpu_khz/1000+1); \
	(unsigned long)delta; \
})

#define ipipe_tsc2us(t) \
({ \
    unsigned long long delta = (t); \
    do_div(delta, cpu_khz/1000+1); \
    (unsigned long)delta; \
})

/* Private interface -- Internal use only */

extern unsigned int cpu_khz;
#define __ipipe_cpu_freq	({ unsigned long long __freq = 1000ULL * cpu_khz; __freq; })
#ifdef CONFIG_X86_TSC
#define __ipipe_hrclock_freq	__ipipe_cpu_freq
#else /* !CONFIG_X86_TSC */
#define __ipipe_hrclock_freq	PIT_TICK_RATE
#endif /* !CONFIG_X86_TSC */

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
	__asm__("bsrl %1, %0":"=r"(ul) : "r"(ul));
	return ul;
}

struct irq_desc;

#define __root_irq_trampoline(__handler__, __regs__)			\
	do {								\
		__asm__ __volatile__("pushfl\n\t"			\
				     "orl   %[x86if],(%%esp)\n\t"	\
				     "pushl %%cs\n\t"			\
				     "pushl $1f\n\t"			\
				     "pushl %%eax\n\t"			\
				     "pushl %%gs\n\t"			\
				     "pushl %%fs\n\t"			\
				     "pushl %%es\n\t"			\
				     "pushl %%ds\n\t"			\
				     "pushl %%eax\n\t"			\
				     "pushl %%ebp\n\t"			\
				     "pushl %%edi\n\t"			\
				     "pushl %%esi\n\t"			\
				     "pushl %%edx\n\t"			\
				     "pushl %%ecx\n\t"			\
				     "pushl %%ebx\n\t"			\
				     "call  *%1\n\t"			\
				     "jmp   ret_from_intr\n\t"		\
				     "1:    cli\n"			\
				     : /* no output */			\
				     : "a" (__regs__),			\
				       "r" (__handler__),		\
				       [x86if] "i" (X86_EFLAGS_IF));	\
	} while (0)

#endif	/* !__X86_IPIPE_32_H */
