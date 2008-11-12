/*
 * Copyright IBM Corp. 2008
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#ifndef CONFIG_64BIT
.globl _mcount
_mcount:
	ahi	%r15,-128
	stm	%r14,%r5,96(%r15)
	la	%r2,0(%r14)
	l	%r3,132(%r15)
	la	%r3,0(%r3)
	bras	%r14,0f
	.long	ftrace_trace_function
0:	l	%r14,0(%r14)
	l	%r14,0(%r14)
	basr	%r14,%r14
	lm	%r14,%r5,96(%r15)
	ahi	%r15,128
	br	%r14

.globl ftrace_stub
ftrace_stub:
	br	%r14

#else /* CONFIG_64BIT */

.globl _mcount
_mcount:
	aghi	%r15,-224
	stmg	%r14,%r5,160(%r15)
	lgr	%r2,%r14
	lg	%r3,232(%r15)
	larl	%r14,ftrace_trace_function
	lg	%r14,0(%r14)
	basr	%r14,%r14
	lmg	%r14,%r5,160(%r15)
	aghi	%r15,224
	br	%r14

.globl ftrace_stub
ftrace_stub:
	br	%r14

#endif /* CONFIG_64BIT */
