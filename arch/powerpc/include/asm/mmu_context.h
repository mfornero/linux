#ifndef __ASM_POWERPC_MMU_CONTEXT_H
#define __ASM_POWERPC_MMU_CONTEXT_H
#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/mmu.h>	
#include <asm/cputable.h>
#include <asm-generic/mm_hooks.h>
#include <asm/cputhreads.h>

/*
 * Most if the context management is out of line
 */
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void destroy_context(struct mm_struct *mm);

extern void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next);
extern void switch_stab(struct task_struct *tsk, struct mm_struct *mm);
extern void switch_slb(struct task_struct *tsk, struct mm_struct *mm);
extern void set_context(unsigned long id, pgd_t *pgd);

#ifdef CONFIG_PPC_BOOK3S_64
extern int __init_new_context(void);
extern void __destroy_context(int context_id);
static inline void mmu_context_init(void) { }
#else
extern unsigned long __init_new_context(void);
extern void __destroy_context(unsigned long context_id);
extern void mmu_context_init(void);
#endif

extern void switch_cop(struct mm_struct *next);
extern int use_cop(unsigned long acop, struct mm_struct *mm);
extern void drop_cop(unsigned long acop, struct mm_struct *mm);

#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH

#if defined(CONFIG_PPC_MMU_NOHASH) && defined(CONFIG_SMP)
#define __IPIPE_ATOMIC_MM_UPDATE  1
#endif

static inline void __mmactivate_head(void)
{
#ifdef __IPIPE_ATOMIC_MM_UPDATE
	hard_local_irq_disable();
#else
	preempt_disable();
#endif
	__this_cpu_write(ipipe_percpu.active_mm, NULL);
}

static inline void __mmactivate_tail(void)
{
#ifdef __IPIPE_ATOMIC_MM_UPDATE
	hard_local_irq_enable();
#else
	preempt_enable();
#endif
}

#else  /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

static inline void __mmactivate_head(void)
{
#ifdef CONFIG_IPIPE_DEBUG_INTERNAL
	WARN_ON_ONCE(hard_irqs_disabled());
#endif
	hard_cond_local_irq_disable();
}

static inline void __mmactivate_tail(void)
{
	hard_cond_local_irq_enable();
}

#endif  /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */

/*
 * mmu_context_nohash in SMP mode is tracking an activity
 * counter into the mm struct. Therefore, we make sure the
 * kernel always sees the ipipe_percpu.active_mm update and
 * the actual switch as a single atomic operation. Since the
 * related code already requires to hard disable irqs all
 * through the switch, there is no additional penalty anyway.
 */
static inline void __do_switch_mm(struct mm_struct *prev, struct mm_struct *next,
				  struct task_struct *tsk)
{
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	unsigned long flags;
#ifdef __IPIPE_ATOMIC_MM_UPDATE
	flags = hard_local_irq_save();
#endif
	__this_cpu_write(ipipe_percpu.active_mm, NULL);
#else /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
	WARN_ON_ONCE(!hard_irqs_disabled());
#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
	barrier();

	/* The actual HW switching method differs between the various
	 * sub architectures.
	 */
#ifdef CONFIG_PPC_STD_MMU_64
	/* mm state is undefined. */
	if (mmu_has_feature(MMU_FTR_SLB))
		switch_slb(tsk, next);
	else
		switch_stab(tsk, next);
#else
	/* Out of line for now */
	switch_mmu_context(prev, next);
#endif
	barrier();
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
#ifndef __IPIPE_ATOMIC_MM_UPDATE
	flags = hard_local_irq_save();
#endif
	__this_cpu_write(ipipe_percpu.active_mm, next);
#endif  /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

static inline void __switch_mm_prepare(struct mm_struct *prev, struct mm_struct *next,
				       struct task_struct *tsk)
{
	int cpu = ipipe_processor_id();

	/* Mark this context has been used on the new CPU */
	cpumask_set_cpu(cpu, mm_cpumask(next));

	/* 32-bit keeps track of the current PGDIR in the thread struct */
#ifdef CONFIG_PPC32
	tsk->thread.pgdir = next->pgd;
#endif /* CONFIG_PPC32 */

	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = next->pgd;
#endif
	/* Nothing else to do if we aren't actually switching */
	if (prev == next)
		return;

#ifdef CONFIG_PPC_ICSWX
	/* Switch coprocessor context only if prev or next uses a coprocessor */
	if (prev->context.acop || next->context.acop)
		switch_cop(next);
#endif /* CONFIG_PPC_ICSWX */

	/* We must stop all altivec streams before changing the HW
	 * context
	 */
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		asm volatile ("dssall");
#endif /* CONFIG_ALTIVEC */
}

/*
 * __switch_mm is the low level mm switching code, assuming that hw
 * IRQs are off.
 */
static inline void __switch_mm(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk)
{
	__switch_mm_prepare(prev, next, tsk);
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	for (;;) {
		/* Returns with hw IRQs off. */
		__do_switch_mm(prev, next, tsk);
		if (!test_and_clear_thread_flag(TIF_MMSWITCH_INT)) {
			hard_local_irq_enable();
			break;
		}
		hard_local_irq_enable();
	}
#else /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
	__do_switch_mm(prev, next, tsk);
#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

/*
 * switch_mm is the entry point called from the architecture independent
 * code in kernel/sched.c.
 */
static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
#ifndef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	unsigned long flags;
	flags = hard_local_irq_save();
#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
	__switch_mm(prev, next, tsk);
#ifndef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	hard_local_irq_restore(flags);
#endif /* !CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH */
}

/*
 * ipipe_switch_mm_head is reserved to the head domain for switching
 * mmu context.
 */
static inline
void ipipe_switch_mm_head(struct mm_struct *prev, struct mm_struct *next,
			  struct task_struct *tsk)
{
	__switch_mm_prepare(prev, next, tsk);
	__do_switch_mm(prev, next, tsk);
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
#ifndef CONFIG_IPIPE
	unsigned long flags;

	local_irq_save(flags);
#endif
	__switch_mm(prev, next, current);
#ifndef CONFIG_IPIPE
	local_irq_restore(flags);
#endif
}

/* We don't currently use enter_lazy_tlb() for anything */
static inline void enter_lazy_tlb(struct mm_struct *mm,
				  struct task_struct *tsk)
{
	/* 64-bit Book3E keeps track of current PGD in the PACA */
#ifdef CONFIG_PPC_BOOK3E_64
	get_paca()->pgd = NULL;
#endif
}

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_MMU_CONTEXT_H */
