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
#include <asm/system_misc.h>	/* For user_debug, UDBG_SEGV */

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

unsigned long
fcse_check_mmap_inner(struct mm_struct *mm,
		      struct vm_unmapped_area_info *info, 
		      unsigned long addr, unsigned long flags)
{
	if (flags & MAP_FIXED)
		goto skip_retry;
 
        /* Try again the mmap, allowing addresses above 32 MB */
	info->flags = 0;
        info->low_limit = PAGE_ALIGN(mm->start_stack);
	info->high_limit = FCSE_TASK_SIZE;
        addr = vm_unmapped_area(info);
 
	if ((addr & ~PAGE_MASK) == 0 && addr + info->length <= FCSE_TASK_SIZE)
                return addr;
        
	/* Could not find an address */
  skip_retry:
#ifdef CONFIG_ARM_FCSE_MESSAGES
	if ((flags & MAP_BRK) == 0)
		printk(KERN_WARNING 
		       "FCSE: process %u(%s) VM would exceed the 32MB limit.\n",
		       current->pid, current->comm);
#endif /* CONFIG_ARM_FCSE_MESSAGES */
	return -ENOMEM;
}

#ifdef CONFIG_ARM_FCSE_MESSAGES
#define addr_in_vma(vma, addr)						\
	({								\
		struct vm_area_struct *_vma = (vma);			\
		((unsigned long)((addr) - _vma->vm_start)		\
		 < (unsigned long)((_vma->vm_end - _vma->vm_start)));	\
	})

#ifdef CONFIG_DEBUG_USER
static noinline void
dump_vmas(struct mm_struct *mm, unsigned long addr, struct pt_regs *regs)
{
	struct vm_area_struct *vma;
	char path[128];
	int locked = 0;

	printk("mappings:\n");
	if (!in_atomic())
		locked = down_read_trylock(&mm->mmap_sem);
	for(vma = mm->mmap; vma; vma = vma->vm_next) {
		struct file *file = vma->vm_file;
		int flags = vma->vm_flags;
		const char *name;

		printk("0x%08lx-0x%08lx %c%c%c%c 0x%08llx ",
		       vma->vm_start,
		       vma->vm_end,
		       flags & VM_READ ? 'r' : '-',
		       flags & VM_WRITE ? 'w' : '-',
		       flags & VM_EXEC ? 'x' : '-',
		       flags & VM_MAYSHARE ? 's' : 'p',
		       ((loff_t)vma->vm_pgoff) << PAGE_SHIFT);

		if (file)
			name = d_path(&file->f_path, path, sizeof(path));
		else if ((name = arch_vma_name(vma)))
			;
		else if (!vma->vm_mm)
			name = "[vdso]";
		else if (vma->vm_start <= mm->start_brk
			 && vma->vm_end >= mm->brk)
			name = "[heap]";
		else if (vma->vm_start <= mm->start_stack &&
			 vma->vm_end >= mm->start_stack)
			name = "[stack]";
		else
			name = "";
		printk("%s", name);
		if (addr_in_vma(vma, regs->ARM_pc))
			printk(" <- PC");
		if (addr_in_vma(vma, regs->ARM_sp))
			printk(" <- SP");
		if (addr_in_vma(vma, addr))
			printk("%s fault",
			       (addr_in_vma(vma, regs->ARM_pc)
				|| addr_in_vma(vma, regs->ARM_sp)
				? "," : " <-"));
		printk("\n");
	}
	if (locked)
		up_read(&mm->mmap_sem);
}
#endif /* CONFIG_DEBUG_USER */

void fcse_notify_segv(struct mm_struct *mm,
		       unsigned long addr, struct pt_regs *regs)
{
	int locked = 0;

#if defined(CONFIG_DEBUG_USER)
	if (user_debug & UDBG_SEGV)
		dump_vmas(mm, addr, regs);
#endif /* CONFIG_DEBUG_USER */

	if (!in_atomic())
		locked = down_read_trylock(&mm->mmap_sem);
	if (find_vma(mm, addr) == find_vma(mm, regs->ARM_sp))
		printk(KERN_INFO "FCSE: process %u(%s) probably overflowed stack at 0x%08lx.\n",
		       current->pid, current->comm, regs->ARM_pc);
	if (locked)
		up_read(&mm->mmap_sem);
}
#endif /* CONFIG_ARM_FCSE_MESSAGES */
