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

int fcse_pid_alloc(struct mm_struct *mm);
void fcse_pid_free(struct mm_struct *mm);

/* Sets the CPU's PID Register */
static inline void fcse_pid_set(unsigned long pid)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c13, c0, 0"
			      : /* */: "r" (pid) : "cc", "memory");
}

static inline int
fcse_switch_mm(struct mm_struct *next)
{

	if (!cache_is_vivt())
		return 0;

	fcse_pid_set(next->context.fcse.pid);
	return 0;
}

#else /* ! CONFIG_ARM_FCSE */
#define fcse_switch_mm(next) 1
#endif /* ! CONFIG_ARM_FCSE */
#endif /* __ASM_ARM_FCSE_H */
