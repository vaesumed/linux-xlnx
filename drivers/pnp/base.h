extern spinlock_t pnp_lock;
void *pnp_alloc(long size);
struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *, int id, char *pnpid);
struct pnp_card *pnp_alloc_card(struct pnp_protocol *, int id, char *pnpid);
struct pnp_id *pnp_add_id(struct pnp_dev *dev, char *id);
struct pnp_id *pnp_add_card_id(struct pnp_card *card, char *id);
int pnp_interface_attach_device(struct pnp_dev *dev);
void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_option(struct pnp_option *option);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

int pnp_check_port(struct pnp_dev * dev, int idx);
int pnp_check_mem(struct pnp_dev * dev, int idx);
int pnp_check_irq(struct pnp_dev * dev, int idx);
int pnp_check_dma(struct pnp_dev * dev, int idx);

int pnp_add_irq_resource(struct pnp_dev *dev, int irq, int flags);
int pnp_add_dma_resource(struct pnp_dev *dev, int dma, int flags);
int pnp_add_io_resource(struct pnp_dev *dev, resource_size_t start, resource_size_t len, int flags);
int pnp_add_mem_resource(struct pnp_dev *dev, resource_size_t start, resource_size_t len, int flags);
