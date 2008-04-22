#ifndef LINUX__SLUB_KMEMCHECK__H
#define LINUX__SLUB_KMEMCHECK__H

#ifdef CONFIG_KMEMCHECK
struct page *kmemcheck_allocate_slab(struct kmem_cache *s,
	gfp_t flags, int node, int pages);
void kmemcheck_free_slab(struct kmem_cache *s, struct page *page, int pages);

void kmemcheck_slab_alloc(struct kmem_cache *s, gfp_t gfpflags, void *object);
void kmemcheck_slab_free(struct kmem_cache *s, void *object);
#else
static inline struct page *kmemcheck_allocate_slab(struct kmem_cache *s,
	gfp_t flags, int node, int pages) { return NULL; }
static inline void kmemcheck_free_slab(struct kmem_cache *s,
	struct page *page, int pages) { }
static inline void kmemcheck_slab_alloc(struct kmem_cache *s,
	gfp_t gfpflags, void *object) { }
static inline void kmemcheck_slab_free(struct kmem_cache *s, void *object) { }
#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX__SLUB_KMEMCHECK__H */
