#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kmemcheck.h>

struct page *
kmemcheck_allocate_slab(struct kmem_cache *s, gfp_t flags, int node, int pages)
{
	struct page *page;

	/*
	 * With kmemcheck enabled, we actually allocate twice as much. The
	 * upper half of the allocation is used as our shadow memory where
	 * the status (e.g. initialized/uninitialized) of each byte is
	 * stored.
	 */

	flags |= __GFP_COMP;

	if (node == -1)
		page = alloc_pages(flags, s->order + 1);
	else
		page = alloc_pages_node(node, flags, s->order + 1);

	if (!page)
		return NULL;

	/*
	 * Mark it as non-present for the MMU so that our accesses to
	 * this memory will trigger a page fault and let us analyze
	 * the memory accesses.
	 */
	kmemcheck_hide_pages(page, pages);

	/*
	 * Objects from caches that have a constructor don't get
	 * cleared when they're allocated, so we need to do it here.
	 */
	if (s->ctor)
		kmemcheck_mark_uninitialized_pages(page, pages);
	else
		kmemcheck_mark_unallocated_pages(page, pages);

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		pages + pages);

	return page;
}

void
kmemcheck_free_slab(struct kmem_cache *s, struct page *page, int pages)
{
	kmemcheck_show_pages(page, pages);

	__ClearPageSlab(page);

	mod_zone_page_state(page_zone(page),
		(s->flags & SLAB_RECLAIM_ACCOUNT) ?
		NR_SLAB_RECLAIMABLE : NR_SLAB_UNRECLAIMABLE,
		-pages - pages);

	__free_pages(page, s->order + 1);
}

void
kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object)
{
	if (gfpflags & __GFP_ZERO)
		return;
	if (s->flags & SLAB_NOTRACK)
		return;

	if (!kmemcheck_enabled || gfpflags & __GFP_NOTRACK) {
		/*
		 * Allow notracked objects to be allocated from
		 * tracked caches. Note however that these objects
		 * will still get page faults on access, they just
		 * won't ever be flagged as uninitialized. If page
		 * faults are not acceptable, the slab cache itself
		 * should be marked NOTRACK.
		 */
		kmemcheck_mark_initialized(object, s->objsize);
	} else if (!s->ctor) {
		/*
		 * New objects should be marked uninitialized before
		 * they're returned to the called.
		 */
		kmemcheck_mark_uninitialized(object, s->objsize);
	}
}

void
kmemcheck_slab_free(struct kmem_cache *s, void *object)
{
	/* TODO: RCU freeing is unsupported for now; hide false positives. */
	if (!s->ctor && !(s->flags & SLAB_DESTROY_BY_RCU))
		kmemcheck_mark_freed(object, s->objsize);
}
