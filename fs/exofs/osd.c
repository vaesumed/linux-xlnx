/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <scsi/scsi_device.h>
#include <scsi/osd_sense.h>

#include "exofs.h"

int exofs_check_ok(struct osd_request *or)
{
	struct osd_sense_info osi;
	int ret = osd_req_decode_sense(or, &osi);

	if (ret) { /* translate to Linux codes */
		if (osi.additional_code == scsi_invalid_field_in_cdb) {
			if (osi.cdb_field_offset == OSD_CFO_STARTING_BYTE)
				ret = -EFAULT;
			if (osi.cdb_field_offset == OSD_CFO_OBJECT_ID)
				ret = -ENOENT;
			else
				ret = -EINVAL;
		} else if (osi.additional_code == osd_quota_error)
			ret = -ENOSPC;
		else
			ret = -EIO;
	}

	return ret;
}

void exofs_make_credential(u8 cred_a[OSD_CAP_LEN], const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

/*
 * Perform a synchronous OSD operation.
 */
int exofs_sync_op(struct osd_request *or, int timeout, uint8_t *credential)
{
	int ret;

	or->timeout = timeout;
	ret = osd_finalize_request(or, 0, credential, NULL);
	if (ret) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		return ret;
	}

	ret = osd_execute_request(or);

	if (ret)
		EXOFS_DBGMSG("osd_execute_request() => %d\n", ret);
	/* osd_req_decode_sense(or, ret); */
	return ret;
}

/*
 * Perform an asynchronous OSD operation.
 */
int exofs_async_op(struct osd_request *or, osd_req_done_fn *async_done,
		   void *caller_context, u8 *cred)
{
	int ret;

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (ret) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		return ret;
	}

	ret = osd_execute_request_async(or, async_done, caller_context);

	if (ret)
		EXOFS_DBGMSG("osd_execute_request_async() => %d\n", ret);
	return ret;
}

int extract_attr_from_req(struct osd_request *or, struct osd_attr *attr)
{
	struct osd_attr cur_attr = {.attr_page = 0}; /* start with zeros */
	void *iter = NULL;
	int nelem;

	do {
		nelem = 1;
		osd_req_decode_get_attr_list(or, &cur_attr, &nelem, &iter);
		if ((cur_attr.attr_page == attr->attr_page) &&
		    (cur_attr.attr_id == attr->attr_id)) {
			attr->len = cur_attr.len;
			attr->val_ptr = cur_attr.val_ptr;
			return 0;
		}
	} while (iter);

	return -EIO;
}

static void _osd_read(struct osd_request *or,
	const struct osd_obj_id *obj, uint64_t offset, struct bio *bio)
{
	osd_req_read(or, obj, bio, offset);
	EXOFS_DBGMSG("osd_req_read(p=%llX, ob=%llX, l=%llu, of=%llu)\n",
		_LLU(obj->partition), _LLU(obj->id), _LLU(bio->bi_size),
		_LLU(offset));
}

#ifdef __KERNEL__
static struct bio *_bio_map_pages(struct request_queue *req_q,
				  struct page **pages, unsigned page_count,
				  size_t length, gfp_t gfp_mask)
{
	struct bio *bio;
	int i;

	bio = bio_alloc(gfp_mask, page_count);
	if (!bio) {
		EXOFS_DBGMSG("Failed to bio_alloc page_count=%d\n", page_count);
		return NULL;
	}

	for (i = 0; i < page_count && length; i++) {
		size_t use_len = min(length, PAGE_SIZE);

		if (use_len !=
		    bio_add_pc_page(req_q, bio, pages[i], use_len, 0)) {
			EXOFS_ERR("Failed bio_add_pc_page req_q=%p pages[i]=%p "
				  "use_len=%Zd page_count=%d length=%Zd\n",
				  req_q, pages[i], use_len, page_count, length);
			bio_put(bio);
			return NULL;
		}

		length -= use_len;
	}

	WARN_ON(length);
	return bio;
}

int osd_req_read_pages(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, u64 length,
	struct page **pages, int page_count)
{
	struct request_queue *req_q = or->osd_dev->scsi_device->request_queue;
	struct bio *bio = _bio_map_pages(req_q, pages, page_count, length,
					 GFP_KERNEL);

	if (!bio)
		return -ENOMEM;

	_osd_read(or, obj, offset, bio);
	return 0;
}
#endif /* def __KERNEL__ */

int osd_req_read_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void* buff, u64 len)
{
	struct request_queue *req_q = or->osd_dev->scsi_device->request_queue;
	struct bio *bio = bio_map_kern(req_q, buff, len, GFP_KERNEL);

	if (!bio)
		return -ENOMEM;

	_osd_read(or, obj, offset, bio);
	return 0;
}

static void _osd_write(struct osd_request *or,
	const struct osd_obj_id *obj, uint64_t offset, struct bio *bio)
{
	osd_req_write(or, obj, bio, offset);
	EXOFS_DBGMSG("osd_req_write(p=%llX, ob=%llX, l=%llu, of=%llu)\n",
		_LLU(obj->partition), _LLU(obj->id), _LLU(bio->bi_size),
		_LLU(offset));
}

#ifdef __KERNEL__
int osd_req_write_pages(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, u64 length,
	struct page **pages, int page_count)
{
	struct request_queue *req_q = or->osd_dev->scsi_device->request_queue;
	struct bio *bio = _bio_map_pages(req_q, pages, page_count, length,
					 GFP_KERNEL);

	if (!bio)
		return -ENOMEM;

	_osd_write(or, obj, offset, bio);
	return 0;
}
#endif /* def __KERNEL__ */

int osd_req_write_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void* buff, u64 len)
{
	struct request_queue *req_q = or->osd_dev->scsi_device->request_queue;
	struct bio *bio = bio_map_kern(req_q, buff, len, GFP_KERNEL);

	if (!bio)
		return -ENOMEM;

	_osd_write(or, obj, offset, bio);
	return 0;
}
