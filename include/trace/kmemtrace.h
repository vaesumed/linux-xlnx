/*
 * Copyright (C) 2008 Eduard - Gabriel Munteanu
 *
 * This file is released under GPL version 2.
 */

#ifndef _LINUX_KMEMTRACE_H
#define _LINUX_KMEMTRACE_H

#ifdef __KERNEL__

#include <linux/tracepoint.h>
#include <linux/types.h>

extern void kmemtrace_init(void);

DECLARE_TRACE(kmalloc,
	      TPPROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags),
	      TPARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags));
DECLARE_TRACE(kmem_cache_alloc,
	      TPPROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags),
	      TPARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags));
DECLARE_TRACE(kmalloc_node,
	      TPPROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags,
		      int node),
	      TPARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node));
DECLARE_TRACE(kmem_cache_alloc_node,
	      TPPROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags,
		      int node),
	      TPARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node));
DECLARE_TRACE(kfree,
	      TPPROTO(unsigned long call_site, const void *ptr),
	      TPARGS(call_site, ptr));
DECLARE_TRACE(kmem_cache_free,
	      TPPROTO(unsigned long call_site, const void *ptr),
	      TPARGS(call_site, ptr));

#endif /* __KERNEL__ */

#endif /* _LINUX_KMEMTRACE_H */

