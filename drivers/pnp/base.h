extern spinlock_t pnp_lock;
void *pnp_alloc(long size);

int pnp_register_protocol(struct pnp_protocol *protocol);
void pnp_unregister_protocol(struct pnp_protocol *protocol);

struct pnp_dev *pnp_alloc_dev(struct pnp_protocol *, int id, char *pnpid);
struct pnp_card *pnp_alloc_card(struct pnp_protocol *, int id, char *pnpid);

int pnp_add_device(struct pnp_dev *dev);
struct pnp_id *pnp_add_id(struct pnp_dev *dev, char *id);
int pnp_interface_attach_device(struct pnp_dev *dev);

int pnp_add_card(struct pnp_card *card);
struct pnp_id *pnp_add_card_id(struct pnp_card *card, char *id);
void pnp_remove_card(struct pnp_card *card);
int pnp_add_card_device(struct pnp_card *card, struct pnp_dev *dev);
void pnp_remove_card_device(struct pnp_dev *dev);

struct pnp_option *pnp_register_independent_option(struct pnp_dev *dev);
struct pnp_option *pnp_register_dependent_option(struct pnp_dev *dev,
						 int priority);
int pnp_register_irq_resource(struct pnp_option *option, struct pnp_irq *data);
int pnp_register_dma_resource(struct pnp_option *option, struct pnp_dma *data);
int pnp_register_port_resource(struct pnp_option *option,
			       struct pnp_port *data);
int pnp_register_mem_resource(struct pnp_option *option, struct pnp_mem *data);
void pnp_init_resources(struct pnp_dev *dev);

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

#define PNP_MAX_PORT		40
#define PNP_MAX_MEM		24
#define PNP_MAX_IRQ		2
#define PNP_MAX_DMA		2

struct pnp_resource_table {
	struct resource port_resource[PNP_MAX_PORT];
	struct resource mem_resource[PNP_MAX_MEM];
	struct resource dma_resource[PNP_MAX_DMA];
	struct resource irq_resource[PNP_MAX_IRQ];
};
