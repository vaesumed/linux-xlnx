/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 2000 Hewlett-Packard Co, Linuxcare Inc.
 * Copyright (C) 2000 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 * Copyright (C) 2008 Helge Deller <deller@gmx.de>
 * Copyright (C) 2009 Kyle McMartin <kyle@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/user.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/elf.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/asm-offsets.h>

/* PSW bits we allow the debugger to modify */
#define USER_PSW_BITS	(PSW_N | PSW_V | PSW_CB)

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP|TIF_BLOCKSTEP);

	/* make sure the trap bits are not set */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

/*
 * The following functions are called by ptrace_resume() when
 * enabling or disabling single/block tracing.
 */
void user_disable_single_step(struct task_struct *task)
{
	ptrace_disable(task);
}

void user_enable_single_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_BLOCKSTEP);
	set_tsk_thread_flag(task, TIF_SINGLESTEP);

	if (pa_psw(task)->n) {
		struct siginfo si;

		/* Nullified, just crank over the queue. */
		task_regs(task)->iaoq[0] = task_regs(task)->iaoq[1];
		task_regs(task)->iasq[0] = task_regs(task)->iasq[1];
		task_regs(task)->iaoq[1] = task_regs(task)->iaoq[0] + 4;
		pa_psw(task)->n = 0;
		pa_psw(task)->x = 0;
		pa_psw(task)->y = 0;
		pa_psw(task)->z = 0;
		pa_psw(task)->b = 0;
		ptrace_disable(task);
		/* Don't wake up the task, but let the
		   parent know something happened. */
		si.si_code = TRAP_TRACE;
		si.si_addr = (void __user *) (task_regs(task)->iaoq[0] & ~3);
		si.si_signo = SIGTRAP;
		si.si_errno = 0;
		force_sig_info(SIGTRAP, &si, task);
		/* notify_parent(task, SIGCHLD); */
		return;
	}

	/* Enable recovery counter traps.  The recovery counter
	 * itself will be set to zero on a task switch.  If the
	 * task is suspended on a syscall then the syscall return
	 * path will overwrite the recovery counter with a suitable
	 * value such that it traps once back in user space.  We
	 * disable interrupts in the tasks PSW here also, to avoid
	 * interrupts while the recovery counter is decrementing.
	 */
	pa_psw(task)->r = 1;
	pa_psw(task)->t = 0;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

void user_enable_block_step(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
	set_tsk_thread_flag(task, TIF_BLOCKSTEP);

	/* Enable taken branch trap. */
	pa_psw(task)->r = 0;
	pa_psw(task)->t = 1;
	pa_psw(task)->h = 0;
	pa_psw(task)->l = 0;
}

/* extra regs not saved in pt_regs, written when ejecting core */
static inline void fill_specials(unsigned long *regs)
{
	int i = 0;
#define SAVE_CR(cr)	regs[i++] = mfctl(cr)
	SAVE_CR(22);	SAVE_CR( 0);
	SAVE_CR(24);	SAVE_CR(25);
	SAVE_CR(26);	SAVE_CR(27);
	SAVE_CR(28);	SAVE_CR(29);
	SAVE_CR(30);	SAVE_CR(31);
	SAVE_CR( 8);	SAVE_CR( 9);
	SAVE_CR(12);	SAVE_CR(13);
	SAVE_CR(10);	SAVE_CR(15);
#undef SAVE_CR
}

/* save thread state in regset. this does extra work compared to the gr_set
 * function since we save extra magic in coredumps, that we don't let
 * userspace play with. (protection registers and the like.)
 */
static int gr_get(struct task_struct *tsk, const struct user_regset *regset,
	unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(tsk);
	unsigned long cr[16];
	unsigned long *kbuf_reg = kbuf, *ubuf_reg = ubuf;	/* register view */
	int ret;

	/* 32 gprs, %r0 ... %r31 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &regs->gr[0],
		0, 32 * sizeof(unsigned long));
	if (ret)
		goto out;

	/* %sr0 ... %sr7 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &regs->sr[0],
		0, 8 * sizeof(unsigned long));
	if (ret)
		goto out;

	/* extra magic stuff we need for coredumps
	 * sadly we can't just chunk through pt_regs... sigh.
	 */
#define SAVE_REG(r)	({	\
				if (count <= 0)	\
					goto out;	\
				if (kbuf)	\
					*kbuf_reg++ = (r);	\
				else	\
					ret = __put_user((r), ubuf_reg++);	\
					if (ret < 0)	\
						goto out;	\
				++pos, --count;	\
			})

	SAVE_REG(regs->iaoq[0]);	SAVE_REG(regs->iaoq[1]);
	SAVE_REG(regs->iasq[0]);	SAVE_REG(regs->iasq[1]);
	SAVE_REG(regs->sar);		SAVE_REG(regs->iir);
	SAVE_REG(regs->isr);		SAVE_REG(regs->ior);
#undef SAVE_REG
	ubuf = ubuf_reg, kbuf = kbuf_reg;

	fill_specials(cr);
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &cr,
		0, ARRAY_SIZE(cr));

out:
	return ret;
}

/* fill in struct pt_regs from a regset. */
static int gr_set(struct task_struct *tsk, const struct user_regset *regset,
	unsigned int pos, unsigned int count,
	const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	unsigned long psw;
	const unsigned long *ubuf_reg = ubuf, *kbuf_reg = kbuf;
	int ret;

	/* spirit away our PSW, which is in %r0. and only let the user update
	 * USER_PSW_BITS.
	 */
	psw = regs->gr[0];
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &regs->gr[0],
		0, 32 * sizeof(long));
	if (regs->gr[0] != psw)
		regs->gr[0] = (psw & ~USER_PSW_BITS) | (regs->gr[0] & USER_PSW_BITS);
	if (ret || count <= 0)
		goto out;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &regs->sr[0],
		0, 8 * sizeof(long));
	if (ret || count <= 0)
		goto out;

	/* additional magics (iaoq, iasq, ior, etc.) */
#define RESTORE_REG(r)	({	\
				if (count <= 0)	\
					goto out;	\
				if (kbuf)	\
					(r) = *kbuf_reg++;	\
				else {	\
					ret = __get_user((r), ubuf_reg++);	\
					if (ret < 0)	\
						goto out;	\
				}	\
				++pos, --count;	\
			})

	RESTORE_REG(regs->iaoq[0]);	RESTORE_REG(regs->iaoq[1]);
	RESTORE_REG(regs->iasq[0]);	RESTORE_REG(regs->iasq[1]);
	RESTORE_REG(regs->sar);		RESTORE_REG(regs->iir);
	RESTORE_REG(regs->isr);		RESTORE_REG(regs->ior);
#undef RESTORE_REG
	ubuf = ubuf_reg, kbuf = kbuf_reg;

out:
	return ret;
}

/* thankfully our floating point is sensible. */
static int fr_get(struct task_struct *tsk, const struct user_regset *regset,
	unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	int ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &regs->fr[0],
		0, ELF_NFPREG * sizeof(double));

	return ret;
}

static int fr_set(struct task_struct *tsk, const struct user_regset *regset,
	unsigned int pos, unsigned int count,
	const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &regs->fr[0],
		0, ELF_NFPREG * sizeof(double));

	return ret;
}

enum pa_regset {
	REGSET_GR,
	REGSET_FR,
};

static const struct user_regset pa_regsets[] = {
	[REGSET_GR] = {
		.core_note_type	=	NT_PRSTATUS,
		.n		=	ELF_NGREG,
		.size		=	sizeof(long),
		.align		=	sizeof(long),
		.get		=	gr_get,
		.set		=	gr_set,
	},
	[REGSET_FR] = {
		.core_note_type	=	NT_PRFPREG,
		.n		=	ELF_NFPREG,
		.size		=	sizeof(double),
		.align		=	sizeof(double),
		.get		=	fr_get,
		.set		=	fr_set,
	},
};

static const struct user_regset_view user_parisc_native_view = {
	.name		= "parisc",
	.e_machine	= EM_PARISC,
	.ei_osabi	= ELFOSABI_LINUX,
	.regsets	= pa_regsets,
	.n		= ARRAY_SIZE(pa_regsets),
};

#ifdef CONFIG_COMPAT

/* unmitigated bullshit abounds */
static int gr_get_compat(struct task_struct *tsk,
	const struct user_regset *regset,
	unsigned int pos, unsigned int count,
	void *_kbuf, void __user *_ubuf)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	compat_ulong_t *kbuf = _kbuf;
	compat_ulong_t __user *ubuf = _ubuf;
	compat_ulong_t psw, reg;
	unsigned long cr[16];
	int i, ret = 0;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	/* gprs, with some evil */
	psw = regs->gr[0];
	if (kbuf)
		for (i = 0; count > 0 && i < 32; count--, pos++)
			*kbuf++ = (compat_ulong_t)(regs->gr[i++]);
	else
		for (i = 0; count > 0 && i < 32; count--, pos++) {
			ret = __put_user((compat_ulong_t)regs->gr[i++], ubuf++);
			if (ret < 0)
				goto out;
		}
	if (regs->gr[0] != psw)
		regs->gr[0] = (psw & ~USER_PSW_BITS) | (regs->gr[0] & USER_PSW_BITS);

	/* space registers */
	if (kbuf)
		for (i = 0; count > 0 && i < 8; count--, pos++)
			*kbuf++ = (compat_ulong_t)(regs->sr[i++]);
	else
		for (i = 0; count > 0 && i < 8; count--, pos++) {
			ret = __put_user((compat_ulong_t)regs->sr[i++], ubuf++);
			if (ret < 0)
				goto out;
		}

	/* all the other bollocks we need in our coredump */
#define SAVE_REG(r)	({	\
				if (count <= 0)	\
					goto out;	\
				if (kbuf)	\
					*kbuf++ = (compat_ulong_t)((r));	\
				else	\
					ret = __put_user((compat_ulong_t)(r), ubuf++);	\
					if (ret < 0)	\
						goto out;	\
				++pos, --count;	\
			})

	SAVE_REG(regs->iaoq[0]);	SAVE_REG(regs->iaoq[1]);
	SAVE_REG(regs->iasq[0]);	SAVE_REG(regs->iasq[1]);
	SAVE_REG(regs->sar);		SAVE_REG(regs->iir);
	SAVE_REG(regs->isr);		SAVE_REG(regs->ior);

	fill_specials(cr);
	for (i = 0; i < 16; count--, pos++)
		SAVE_REG(cr[i++]);
#undef SAVE_REG
	/* that should bring us up to 64*sizeof(ulong_t || compat_ulong_t) */

	_kbuf = kbuf;
	_ubuf = ubuf;
	pos *= sizeof(reg);
	count *= sizeof(reg);

	ret = user_regset_copyout_zero(&pos, &count, &_kbuf, &_ubuf,
		64 * sizeof(reg), -1);
out:
	return ret;
}

/* thankfully we get to avoid the %cr saving we do in _get, since
 * ptrace doesn't need to touch it.
 */
static int gr_set_compat(struct task_struct *tsk,
	const struct user_regset *regset,
	unsigned int pos, unsigned int count,
	const void *_kbuf, const void __user *_ubuf)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	const compat_ulong_t *kbuf = _kbuf;
	const compat_ulong_t __user *ubuf = _ubuf;
	compat_ulong_t reg;
	int ret = 0;

	/* if i can't smoke and swear, i'm *fucked* */
	pos /= sizeof(reg);
	count /= sizeof(reg);

	/* 32 gprs @ 4-bytes a piece */
        if (kbuf)
                for (; count > 0 && pos < 32; --count)
                        regs->gr[pos++] = *kbuf++;
        else
                for (; count > 0 && pos < 32; --count) {
			ret = __get_user(reg, ubuf++);
			if (ret < 0)
                                goto out;
                        regs->gr[pos++] = reg;
                }

	/* 8 space registers */
	if (kbuf)
		for (; count > 0 && pos < 8; --count)
			regs->sr[pos++] = *kbuf++;
	else
		for (; count > 0 && pos < 8; --count) {
			ret = __get_user(reg, ubuf++);
			if (ret < 0)
				goto out;
			regs->sr[pos++] = reg;
		}

	/* additional magics (iaoq, iasq, ior, etc.) */
#define RESTORE_REG(r)	({	\
				if (count <= 0)	\
					goto out;	\
				if (kbuf)	\
					(r) = *kbuf++;	\
				else {	\
					ret = __get_user(reg, ubuf++);	\
					if (ret < 0)	\
						goto out;	\
					(r) = reg;	\
				}	\
				++pos, --count;	\
			})

	RESTORE_REG(regs->iaoq[0]);	RESTORE_REG(regs->iaoq[1]);
	RESTORE_REG(regs->iasq[0]);	RESTORE_REG(regs->iasq[1]);
	RESTORE_REG(regs->sar);		RESTORE_REG(regs->iir);
	RESTORE_REG(regs->isr);		RESTORE_REG(regs->ior);
#undef RESTORE_REG

	/* update our position */
	_kbuf = kbuf;
	_ubuf = ubuf;

out:
	return ret;
}

static const struct user_regset pa_regsets_compat[] = {
	[REGSET_GR] = {
		.core_note_type	=	NT_PRSTATUS,
		.n		=	ELF_NGREG,
		.size		=	sizeof(compat_long_t),
		.align		=	sizeof(compat_long_t),
		.get		=	gr_get_compat,
		.set		=	gr_set_compat,
	},
	/* no need, fpr are fortunately always 64-bit */
	[REGSET_FR] = {
		.core_note_type	=	NT_PRFPREG,
		.n		=	ELF_NFPREG,
		.size		=	sizeof(double),
		.align		=	sizeof(double),
		.get		=	fr_get,
		.set		=	fr_set,
	},
};

static const struct user_regset_view user_parisc_compat_view = {
	.name		= "parisc",
	.e_machine	= EM_PARISC,
	.ei_osabi	= ELFOSABI_LINUX,
	.regsets	= pa_regsets_compat,
	.n		= ARRAY_SIZE(pa_regsets_compat),
};

#endif /* CONFIG_COMPAT */

const struct user_regset_view *task_user_regset_view(struct task_struct *t)
{
#ifdef CONFIG_COMPAT
	if (__is_compat_task(t))
		return &user_parisc_compat_view;
#endif
	return &user_parisc_native_view;
}

static inline int regset_size(struct task_struct *t, enum pa_regset r)
{
#ifdef CONFIG_COMPAT
	if (__is_compat_task(t))
		return user_parisc_compat_view.regsets[r].n *
			user_parisc_compat_view.regsets[r].size;
#endif
	return user_parisc_native_view.regsets[r].n *
		user_parisc_native_view.regsets[r].size;
}

static inline void __user *regset_ptr(struct task_struct *t, long data)
{
#ifdef CONFIG_COMPAT
	if (__is_compat_task(t))
		return (void __user *)compat_ptr(data);
#endif
	return (void *)data;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	long ret = -EIO;

	switch (request) {

	/* Read the word at location addr in the USER area.  For ptraced
	   processes, the kernel saves all regs on a syscall. */
	case PTRACE_PEEKUSR:
		if ((addr & (sizeof(long)-1)) ||
		    (unsigned long) addr >= sizeof(struct pt_regs))
			break;
		tmp = *(unsigned long *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (unsigned long *) data);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		if (addr == PT_PSW) {
			/* Allow writing to Nullify, Divide-step-correction,
			 * and carry/borrow bits.
			 * BEWARE, if you set N, and then single step, it won't
			 * stop on the nullified instruction.
			 */
			data &= USER_PSW_BITS;
			task_regs(child)->gr[0] &= ~USER_PSW_BITS;
			task_regs(child)->gr[0] |= data;
			ret = 0;
			break;
		}

		if ((addr & (sizeof(long)-1)) ||
		    (unsigned long) addr >= sizeof(struct pt_regs))
			break;
		if ((addr >= PT_GR1 && addr <= PT_GR31) ||
				addr == PT_IAOQ0 || addr == PT_IAOQ1 ||
				(addr >= PT_FR0 && addr <= PT_FR31 + 4) ||
				addr == PT_SAR) {
			*(unsigned long *) ((char *) task_regs(child) + addr) = data;
			ret = 0;
		}
		break;

	case PTRACE_GETREGS:
		return copy_regset_to_user(child, task_user_regset_view(child),
			REGSET_GR, 0, regset_size(child, REGSET_GR),
			regset_ptr(child, data));

	case PTRACE_SETREGS:
		return copy_regset_from_user(child, task_user_regset_view(child),
			REGSET_GR, 0, regset_size(child, REGSET_GR),
			(const void __user *)regset_ptr(child, data));

	case PTRACE_GETFPREGS:
		return copy_regset_to_user(child, task_user_regset_view(child),
			REGSET_FR, 0, regset_size(child, REGSET_FR),
			regset_ptr(child, data));

	case PTRACE_SETFPREGS:
		return copy_regset_from_user(child, task_user_regset_view(child),
			REGSET_FR, 0, regset_size(child, REGSET_FR),
			(const void __user *)regset_ptr(child, data));

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}


#ifdef CONFIG_COMPAT

/* This function is needed to translate 32 bit pt_regs offsets in to
 * 64 bit pt_regs offsets.  For example, a 32 bit gdb under a 64 bit kernel
 * will request offset 12 if it wants gr3, but the lower 32 bits of
 * the 64 bit kernels view of gr3 will be at offset 28 (3*8 + 4).
 * This code relies on a 32 bit pt_regs being comprised of 32 bit values
 * except for the fp registers which (a) are 64 bits, and (b) follow
 * the gr registers at the start of pt_regs.  The 32 bit pt_regs should
 * be half the size of the 64 bit pt_regs, plus 32*4 to allow for fr[]
 * being 64 bit in both cases.
 */

static compat_ulong_t translate_usr_offset(compat_ulong_t offset)
{
	if (offset < 0)
		return sizeof(struct pt_regs);
	else if (offset <= 32*4)	/* gr[0..31] */
		return offset * 2 + 4;
	else if (offset <= 32*4+32*8)	/* gr[0..31] + fr[0..31] */
		return offset + 32*4;
	else if (offset < sizeof(struct pt_regs)/2 + 32*4)
		return offset * 2 + 4 - 32*8;
	else
		return sizeof(struct pt_regs);
}

long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			compat_ulong_t addr, compat_ulong_t data)
{
	compat_uint_t tmp;
	long ret = -EIO;

	switch (request) {

	case PTRACE_PEEKUSR:
		if (addr & (sizeof(compat_uint_t)-1))
			break;
		addr = translate_usr_offset(addr);
		if (addr >= sizeof(struct pt_regs))
			break;

		tmp = *(compat_uint_t *) ((char *) task_regs(child) + addr);
		ret = put_user(tmp, (compat_uint_t *) (unsigned long) data);
		break;

	/* Write the word at location addr in the USER area.  This will need
	   to change when the kernel no longer saves all regs on a syscall.
	   FIXME.  There is a problem at the moment in that r3-r18 are only
	   saved if the process is ptraced on syscall entry, and even then
	   those values are overwritten by actual register values on syscall
	   exit. */
	case PTRACE_POKEUSR:
		/* Some register values written here may be ignored in
		 * entry.S:syscall_restore_rfi; e.g. iaoq is written with
		 * r31/r31+4, and not with the values in pt_regs.
		 */
		if (addr == PT_PSW) {
			/* Since PT_PSW==0, it is valid for 32 bit processes
			 * under 64 bit kernels as well.
			 */
			ret = arch_ptrace(child, request, addr, data);
		} else {
			if (addr & (sizeof(compat_uint_t)-1))
				break;
			addr = translate_usr_offset(addr);
			if (addr >= sizeof(struct pt_regs))
				break;
			if (addr >= PT_FR0 && addr <= PT_FR31 + 4) {
				/* Special case, fp regs are 64 bits anyway */
				*(__u64 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
			else if ((addr >= PT_GR1+4 && addr <= PT_GR31+4) ||
					addr == PT_IAOQ0+4 || addr == PT_IAOQ1+4 ||
					addr == PT_SAR+4) {
				/* Zero the top 32 bits */
				*(__u32 *) ((char *) task_regs(child) + addr - 4) = 0;
				*(__u32 *) ((char *) task_regs(child) + addr) = data;
				ret = 0;
			}
		}
		break;

	case PTRACE_GETREGS:
	case PTRACE_SETREGS:
	case PTRACE_GETFPREGS:
	case PTRACE_SETFPREGS:
		ret = arch_ptrace(child, request, addr, data);
		break;

	default:
		ret = compat_ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}
#endif

long do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		return -1L;

	return regs->gr[20];
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	int stepping = test_thread_flag(TIF_SINGLESTEP|TIF_BLOCKSTEP);

	if (stepping || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, stepping);
}
