/*
 * arch/arm/kernel/fcse.c
 *
 * Helper functions for using the ARM Fast Context Switch Extension with
 * processors supporting it.
 *
 * Copyright (C) 2008 Richard Cochran
 * Copyright (C) 2009-2011 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/memory.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/mman.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/hardirq.h>
#include <linux/export.h>

#include <asm/fcse.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define PIDS_LONGS ((FCSE_NR_PIDS + BITS_PER_LONG - 1) / BITS_PER_LONG)

static DEFINE_RAW_SPINLOCK(fcse_lock);
static unsigned long fcse_pids_bits[PIDS_LONGS];
unsigned long fcse_pids_cache_dirty[PIDS_LONGS];
EXPORT_SYMBOL(fcse_pids_cache_dirty);

static inline void fcse_pid_reference_inner(unsigned fcse_pid)
{
	__set_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_bits);
}

static inline void fcse_pid_dereference(struct mm_struct *mm)
{
	unsigned fcse_pid = mm->context.fcse.pid >> FCSE_PID_SHIFT;

	__clear_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_bits);
	__clear_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_cache_dirty);
}

static inline long find_free_pid(unsigned long bits[])
{
	return FCSE_PID_MAX - find_first_zero_bit(bits, FCSE_NR_PIDS);
}

void fcse_pid_free(struct mm_struct *mm)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid_dereference(mm);
	raw_spin_unlock_irqrestore(&fcse_lock, flags);
}

int fcse_pid_alloc(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned fcse_pid;

	raw_spin_lock_irqsave(&fcse_lock, flags);
	fcse_pid = find_free_pid(fcse_pids_bits);
	if (fcse_pid == -1) {
		raw_spin_unlock_irqrestore(&fcse_lock, flags);
#ifdef CONFIG_ARM_FCSE_MESSAGES
		printk(KERN_WARNING "FCSE: %s[%d] would exceed the %lu processes limit.\n",
		       current->comm, current->pid, FCSE_NR_PIDS);
#endif /* CONFIG_ARM_FCSE_MESSAGES */
		return -EAGAIN;
	}
	fcse_pid_reference_inner(fcse_pid);
	raw_spin_unlock_irqrestore(&fcse_lock, flags);

	return fcse_pid;
}

static inline void fcse_clear_dirty_all(void)
{
	switch(ARRAY_SIZE(fcse_pids_cache_dirty)) {
	case 3:
		fcse_pids_cache_dirty[2] = 0UL;
	case 2:
		fcse_pids_cache_dirty[1] = 0UL;
	case 1:
		fcse_pids_cache_dirty[0] = 0UL;
	}
}

unsigned fcse_flush_all_start(void)
{
	if (!cache_is_vivt())
		return 0;

#ifdef CONFIG_ARM_FCSE_PREEMPT_FLUSH
	return nr_context_switches();
#else
	preempt_disable();
	return 0;
#endif
}

noinline void
fcse_flush_all_done(unsigned seq, unsigned dirty)
{
	unsigned long flags;

	if (!cache_is_vivt())
		return;

	raw_spin_lock_irqsave(&fcse_lock, flags);
#ifdef CONFIG_ARM_FCSE_PREEMPT_FLUSH
	if (seq == nr_context_switches())
#endif /* CONFIG_ARM_FCSE_PREEMPT_FLUSH */
		fcse_clear_dirty_all();

	if (dirty && current->mm != &init_mm && current->mm) {
		unsigned fcse_pid =
			current->mm->context.fcse.pid >> FCSE_PID_SHIFT;
		__set_bit(FCSE_PID_MAX - fcse_pid, fcse_pids_cache_dirty);
	}
	raw_spin_unlock_irqrestore(&fcse_lock, flags);
#ifndef CONFIG_ARM_FCSE_PREEMPT_FLUSH
	preempt_enable();
#endif /* CONFIG_ARM_FCSE_PREEMPT_FLUSH */
}
