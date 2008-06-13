/*
 * Compact binary representation of ihex records. Some devices need their
 * firmware loaded in strange orders rather than a single big blob, but 
 * actually parsing ihex-as-text within the kernel seems silly. Thus,...
 */

#ifndef __LINUX_IHEX_H__
#define __LINUX_IHEX_H__

#include <linux/types.h>
#include <linux/firmware.h>
#include <linux/device.h>

struct ihex_binrec {
	__be32 addr;
	uint8_t len;
	uint8_t data[0];
} __attribute__((aligned(4)));

/* Find the next record, taking into account the 4-byte alignment */
static inline const struct ihex_binrec *
ihex_next_binrec(const struct ihex_binrec *rec)
{
	int next = ((rec->len + 4) & ~3) - 1;
	rec = (void *)&rec->data[next];

	return rec->len ? rec : NULL;
}

/* Check that ihex_next_binrec() won't take us off the end of the image... */
static inline int ihex_validate_fw(const struct firmware *fw)
{
	const struct ihex_binrec *rec, *end;

	rec = (void *)fw->data;
	end = (void *)fw->data + fw->size - 4;

	while (rec) {
		if (rec >= end)
			return -EINVAL;
		rec = ihex_next_binrec(rec);
	}
	return 0;
}

/* Request firmware and validate it so that we can trust we won't
 * run off the end while reading records... */
static inline int request_ihex_firmware(const struct firmware **fw,
					const char *fw_name,
					struct device *dev)
{
	const struct firmware *lfw;
	int ret;

	ret = request_firmware(&lfw, fw_name, dev);
	if (ret)
		return ret;
	ret = ihex_validate_fw(lfw);
	if (ret) {
		dev_err(dev, "Firmware \"%s\" not valid IHEX records\n",
			fw_name);
		release_firmware(lfw);
		return ret;
	}
	*fw = lfw;
	return 0;
}
#endif /* __LINUX_IHEX_H__ */
