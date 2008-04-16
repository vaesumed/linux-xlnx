#ifndef LINUX_KMEMCHECK_H
#define LINUX_KMEMCHECK_H

#ifdef CONFIG_KMEMCHECK
extern int kmemcheck_enabled;

void kmemcheck_init(void);

void kmemcheck_show_pages(struct page *p, unsigned int n);
void kmemcheck_hide_pages(struct page *p, unsigned int n);

void kmemcheck_mark_unallocated(void *address, unsigned int n);
void kmemcheck_mark_uninitialized(void *address, unsigned int n);
void kmemcheck_mark_initialized(void *address, unsigned int n);
void kmemcheck_mark_freed(void *address, unsigned int n);

void kmemcheck_mark_unallocated_pages(struct page *p, unsigned int n);
void kmemcheck_mark_uninitialized_pages(struct page *p, unsigned int n);
#endif /* CONFIG_KMEMCHECK */


#ifndef CONFIG_KMEMCHECK
#define kmemcheck_enabled 0
static inline void kmemcheck_init(void) { }
#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX_KMEMCHECK_H */
