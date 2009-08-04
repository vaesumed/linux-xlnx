#ifndef _PARISC_PTRACE_H
#define _PARISC_PTRACE_H

/* written by Philipp Rumpf, Copyright (C) 1999 SuSE GmbH Nuernberg
** Copyright (C) 2000 Grant Grundler, Hewlett-Packard
*/

#include <linux/types.h>

/* This struct defines the way the registers are stored on the 
 * stack during a system call.
 *
 * N.B. gdb/strace care about the size and offsets within this
 * structure. If you change things, you may break object compatibility
 * for those applications.
 */

struct pt_regs {
	unsigned long gr[32];	/* PSW is in gr[0] */
	__u64 fr[32];
	unsigned long sr[ 8];
	unsigned long iasq[2];
	unsigned long iaoq[2];
	unsigned long cr27;
	unsigned long pad0;     /* available for other uses */
	unsigned long orig_r28;
	unsigned long ksp;
	unsigned long kpc;
	unsigned long sar;	/* CR11 */
	unsigned long iir;	/* CR19 */
	unsigned long isr;	/* CR20 */
	unsigned long ior;	/* CR21 */
	unsigned long ipsw;	/* CR22 */
};

/* regset as seen by PTRACE_{GET|SET}REGS and coredumps */
struct user_regset_struct {
	unsigned long gr[32];
	unsigned long sr[ 8];
	unsigned long iaoq[2];
	unsigned long iasq[2];
	unsigned long sar;
	unsigned long iir;
	unsigned long isr;
	unsigned long ior;
	unsigned long cr22;
	unsigned long  cr0;
	unsigned long cr24;
	unsigned long cr25;
	unsigned long cr26;
	unsigned long cr27;
	unsigned long cr28;
	unsigned long cr29;
	unsigned long cr30;
	unsigned long cr31;
	unsigned long  cr8;
	unsigned long  cr9;
	unsigned long cr12;
	unsigned long cr13;
	unsigned long cr10;
	unsigned long cr15;
	unsigned long __pad0[16];	/* ELF_NGREG is 80, pad it out */
};

/*
 * The numbers chosen here are somewhat arbitrary but absolutely MUST
 * not overlap with any of the number assigned in <linux/ptrace.h>.
 *
 * These ones are taken from IA-64 on the assumption that theirs are
 * the most correct (and we also want to support PTRACE_SINGLEBLOCK
 * since we have taken branch traps too)
 */
#define PTRACE_SINGLEBLOCK	12	/* resume execution until next branch */
#define PTRACE_GETREGS		13
#define PTRACE_SETREGS		14
#define PTRACE_GETFPREGS	18
#define PTRACE_SETFPREGS	19

#ifdef __KERNEL__

#define task_regs(task) ((struct pt_regs *) ((char *)(task) + TASK_REGS))

struct task_struct;
#define arch_has_single_step()	1
void user_disable_single_step(struct task_struct *task);
void user_enable_single_step(struct task_struct *task);

#define arch_has_block_step()	1
void user_enable_block_step(struct task_struct *task);

/* XXX should we use iaoq[1] or iaoq[0] ? */
#define user_mode(regs)			(((regs)->iaoq[0] & 3) ? 1 : 0)
#define user_space(regs)		(((regs)->iasq[1] != 0) ? 1 : 0)
#define instruction_pointer(regs)	((regs)->iaoq[0] & ~3)
#define user_stack_pointer(regs)	((regs)->gr[30])
unsigned long profile_pc(struct pt_regs *);
extern void show_regs(struct pt_regs *);


#endif /* __KERNEL__ */

#endif
