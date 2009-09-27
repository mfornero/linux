/*
 * arch/arm/include/asm/fcse.h
 *
 * Helper header for using the ARM Fast Context Switch Extension with
 * processors supporting it, lifted from the Fast Address Space
 * Switching (FASS) patch for ARM Linux.
 *
 * Copyright (C) 2001, 2002 Adam Wiggins <awiggins@cse.unsw.edu.au>
 * Copyright (C) 2007 Sebastian Smolorz <ssm@emlix.com>
 * Copyright (C) 2008 Richard Cochran
 * Copyright (C) 2009-2011 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_FCSE_H
#define __ASM_ARM_FCSE_H

#ifdef CONFIG_ARM_FCSE

#include <linux/mm_types.h>	/* For struct mm_struct */
#include <linux/sched.h>
#include <linux/hardirq.h>

#include <asm/bitops.h>
#include <asm/cachetype.h>

#define FCSE_PID_SHIFT 25

/* Size of PID relocation area */
#define FCSE_PID_TASK_SIZE (1UL << FCSE_PID_SHIFT)

/* Mask to get rid of PID from relocated address */
#define FCSE_PID_MASK (FCSE_PID_TASK_SIZE - 1)

#define FCSE_PID_INVALID (~0 << FCSE_PID_SHIFT)

#define FCSE_NR_PIDS (TASK_SIZE / FCSE_PID_TASK_SIZE)
#define FCSE_PID_MAX (FCSE_NR_PIDS - 1)

#ifdef CONFIG_ARM_FCSE_DEBUG
#define FCSE_BUG_ON(expr) BUG_ON(expr)
#else /* !CONFIG_ARM_FCSE_DEBUG */
#define FCSE_BUG_ON(expr) do { } while(0)
#endif /* !CONFIG_ARM_FCSE_DEBUG */

extern unsigned long fcse_pids_cache_dirty[];

int fcse_pid_alloc(struct mm_struct *mm);
void fcse_pid_free(struct mm_struct *mm);
unsigned fcse_flush_all_start(void);
void fcse_flush_all_done(unsigned seq, unsigned dirty);

/* Sets the CPU's PID Register */
static inline void fcse_pid_set(unsigned long pid)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c13, c0, 0"
			      : /* */: "r" (pid) : "cc", "memory");
}

static inline unsigned long fcse_mva_to_va(unsigned long mva)
{
	unsigned long va;

	if (!cache_is_vivt())
		return mva;

	__asm__ __volatile__ ("mrc p15, 0, %0, c13, c0, 0"
			      : "=r"(va) : /* */ : "cc", "memory");
	va ^= mva;
	return (va & 0xfe000000) ? mva : va;
}

static inline unsigned long
fcse_va_to_mva(struct mm_struct *mm, unsigned long va)
{
	if (cache_is_vivt() && va < FCSE_PID_TASK_SIZE) {
		return mm->context.fcse.pid | va;
	}
	return va;
}

static inline int
fcse_switch_mm(struct mm_struct *next)
{
	unsigned fcse_pid;

	if (!cache_is_vivt())
		return 0;

	fcse_pid = next->context.fcse.pid >> FCSE_PID_SHIFT;
	set_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_cache_dirty);
	fcse_pid_set(next->context.fcse.pid);
	return 0;
}

static inline int fcse_mm_in_cache(struct mm_struct *mm)
{
	unsigned fcse_pid = mm->context.fcse.pid >> FCSE_PID_SHIFT;
	return test_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_cache_dirty);
}

static inline void fcse_mark_dirty(struct mm_struct *mm)
{
	if (cache_is_vivt()) {
		set_bit(FCSE_PID_MAX - (mm->context.fcse.pid >> FCSE_PID_SHIFT),
			fcse_pids_cache_dirty);
		FCSE_BUG_ON(!fcse_mm_in_cache(mm));
	}
}

#else /* ! CONFIG_ARM_FCSE */
#define fcse_switch_mm(next) 1
#define fcse_mva_to_va(mva) (mva)
#define fcse_va_to_mva(mm, x) ({ (void)(mm); (x); })
#define fcse_mark_dirty(mm) do { (void)(mm); } while(0)
#define fcse_flush_all_start() (0)
#define fcse_flush_all_done(seq, dirty) do { (void)(seq); } while (0)
#define fcse_mm_in_cache(mm) \
		(cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm)))
#endif /* ! CONFIG_ARM_FCSE */
#endif /* __ASM_ARM_FCSE_H */
