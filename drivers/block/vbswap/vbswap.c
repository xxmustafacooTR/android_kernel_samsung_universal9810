// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual block swap device based on vnswap
 *
 * Copyright (C) 2020 Park Ju Hyung
 * Copyright (C) 2013 SungHwan Yun
 */

#define pr_fmt(fmt) "vnswap: " fmt

// #define DEBUG

#include <linux/module.h>
#include <linux/blkdev.h>

#define SECTOR_SHIFT		9
#define SECTOR_SIZE		(1 << SECTOR_SHIFT)
#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define VNSWAP_LOGICAL_BLOCK_SHIFT 12
#define VNSWAP_LOGICAL_BLOCK_SIZE	(1 << VNSWAP_LOGICAL_BLOCK_SHIFT)
#define VNSWAP_SECTOR_PER_LOGICAL_BLOCK	(1 << \
	(VNSWAP_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))

// vnswap is intentionally designed to expose 1 disk only

/* Globals */
static int vnswap_major;
static struct gendisk *vnswap_disk;
static u64 vnswap_disksize;
static struct page *swap_header_page;
static bool vnswap_initialized;

/*
 * Check if request is within bounds and aligned on vnswap logical blocks.
 */
static inline int vnswap_valid_io_request(struct bio *bio)
{
	if (unlikely(
		(bio->bi_iter.bi_sector >= (vnswap_disksize >> SECTOR_SHIFT)) ||
		(bio->bi_iter.bi_sector & (VNSWAP_SECTOR_PER_LOGICAL_BLOCK - 1)) ||
		(bio->bi_iter.bi_size & (VNSWAP_LOGICAL_BLOCK_SIZE - 1)))) {

		return 0;
	}

	/* I/O request is valid */
	return 1;
}

static int vnswap_bvec_read(struct bio_vec *bvec,
			    u32 index, struct bio *bio)
{
	struct page *page;
	unsigned char *user_mem, *swap_header_page_mem;

	if (unlikely(index != 0)) {
		pr_err("tried to read outside of swap header\n");
		// Return empty pages on valid requests to workaround toybox binary search
	}

	page = bvec->bv_page;

	user_mem = kmap_atomic(page);
	if (index == 0 && swap_header_page) {
		swap_header_page_mem = kmap_atomic(swap_header_page);
		memcpy(user_mem + bvec->bv_offset, swap_header_page_mem, bvec->bv_len);
		kunmap_atomic(swap_header_page_mem);

		// It'll be read one-time only
		__free_page(swap_header_page);
		swap_header_page = NULL;
	} else {
		// Do not allow memory dumps
		memset(user_mem + bvec->bv_offset, 0, bvec->bv_len);
	}
	kunmap_atomic(user_mem);
	flush_dcache_page(page);

	return 0;
}

static int vnswap_bvec_write(struct bio_vec *bvec,
			     u32 index, struct bio *bio)
{
	struct page *page;
	unsigned char *user_mem, *swap_header_page_mem;

	if (unlikely(index != 0)) {
		pr_err("tried to write outside of swap header\n");
		return -EIO;
	}

	page = bvec->bv_page;

	user_mem = kmap_atomic(page);
	if (swap_header_page == NULL)
		swap_header_page = alloc_page(GFP_KERNEL | GFP_NOIO);
	swap_header_page_mem = kmap_atomic(swap_header_page);
	memcpy(swap_header_page_mem, user_mem, PAGE_SIZE);
	kunmap_atomic(swap_header_page_mem);
	kunmap_atomic(user_mem);

	return 0;
}

static int vnswap_bvec_rw(struct bio_vec *bvec,
			  u32 index, struct bio *bio, int rw)
{
	if (rw == READ)
		return vnswap_bvec_read(bvec, index, bio);
	else
		return vnswap_bvec_write(bvec, index, bio);
}

static noinline void __vnswap_make_request(struct bio *bio, int rw)
{
	int offset, ret;
	u32 index;
	struct bio_vec bvec;
	struct bvec_iter iter;

	if (!vnswap_valid_io_request(bio)) {
		pr_err("%s %d: invalid io request. "
		       "(bio->bi_iter.bi_sector, bio->bi_iter.bi_size,"
		       "vnswap_disksize) = "
		       "(%llu, %d, %llu)\n",
		       __func__, __LINE__,
		       (unsigned long long)bio->bi_iter.bi_sector,
		       bio->bi_iter.bi_size, vnswap_disksize);

		bio_io_error(bio);
		return;
	}

	index = bio->bi_iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (bio->bi_iter.bi_sector & (SECTORS_PER_PAGE - 1)) <<
	    SECTOR_SHIFT;

	pr_debug("%s %d: (rw, index, offset, bi_size) = "
		 "(%d, %d, %d, %d)\n",
		 __func__, __LINE__, rw, index, offset, bio->bi_iter.bi_size);

	if (offset) {
		pr_err("%s %d: invalid offset. "
		       "(bio->bi_iter.bi_sector, index, offset) = (%llu, %d, %d)\n",
		       __func__, __LINE__,
		       (unsigned long long)bio->bi_iter.bi_sector,
		       index, offset);
		goto out_error;
	}

	if (bio->bi_iter.bi_size > PAGE_SIZE) {
		goto out_error;
	}

	if (bio->bi_vcnt > 1) {
		goto out_error;
	}

	bio_for_each_segment(bvec, bio, iter) {
		if (bvec.bv_len != PAGE_SIZE || bvec.bv_offset != 0) {
			pr_err("%s %d: bvec is misaligned. "
			       "(bv_len, bv_offset) = (%d, %d)\n",
			       __func__, __LINE__, bvec.bv_len, bvec.bv_offset);
			goto out_error;
		}

		pr_debug("%s %d: (rw, index, bvec.bv_len) = "
			 "(%d, %d, %d)\n",
			 __func__, __LINE__, rw, index, bvec.bv_len);

		ret = vnswap_bvec_rw(&bvec, index, bio, rw);
		if (ret < 0) {
			if (ret != -ENOSPC)
				pr_err("%s %d: vnswap_bvec_rw failed."
				       "(ret) = (%d)\n",
				       __func__, __LINE__, ret);
			else
				pr_debug("%s %d: vnswap_bvec_rw failed. "
					 "(ret) = (%d)\n",
					 __func__, __LINE__, ret);
			goto out_error;
		}

		index++;
	}

	bio->bi_error = 0;
	bio_endio(bio);

	return;

out_error:
	bio_io_error(bio);
}

/*
 * Handler function for all vnswap I/O requests.
 */
static blk_qc_t vnswap_make_request(struct request_queue *queue,
				    struct bio *bio)
{
	// Deliberately error out on kernel swap
	if (likely(current->flags & PF_KTHREAD))
		bio_io_error(bio);
	else
		__vnswap_make_request(bio, bio_data_dir(bio));

	return BLK_QC_T_NONE;
}

static const struct block_device_operations vnswap_fops = {
	.owner = THIS_MODULE
};

static ssize_t disksize_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", vnswap_disksize);
}

static ssize_t disksize_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t len)
{
	int ret;
	u64 disksize;

	ret = kstrtoull(buf, 10, &disksize);
	if (ret)
		return ret;

	if (vnswap_initialized) {
		pr_err("already initialized (disksize = %llu)\n", vnswap_disksize);
		return -EBUSY;
	}

	vnswap_disksize = PAGE_ALIGN(disksize);
	if (!vnswap_disksize) {
		pr_err("disksize is invalid (disksize = %llu)\n", vnswap_disksize);

		vnswap_disksize = 0;
		vnswap_initialized = 0;

		return -EINVAL;
	}

	set_capacity(vnswap_disk, vnswap_disksize >> SECTOR_SHIFT);

	vnswap_initialized = 1;

	return len;
}

static DEVICE_ATTR(disksize, S_IRUGO | S_IWUSR, disksize_show, disksize_store);

static struct attribute *vnswap_disk_attrs[] = {
	&dev_attr_disksize.attr,
	NULL,
};

static struct attribute_group vnswap_disk_attr_group = {
	.attrs = vnswap_disk_attrs,
};

static int create_device(void)
{
	int ret;

	/* gendisk structure */
	vnswap_disk = alloc_disk(1);
	if (!vnswap_disk) {
		pr_err("%s %d: Error allocating disk structure for device\n",
		       __func__, __LINE__);
		ret = -ENOMEM;
		goto out;
	}

	vnswap_disk->queue = blk_alloc_queue(GFP_KERNEL);
	if (!vnswap_disk->queue) {
		pr_err("%s %d: Error allocating disk queue for device\n",
		       __func__, __LINE__);
		ret = -ENOMEM;
		goto out_put_disk;
	}

	blk_queue_make_request(vnswap_disk->queue, vnswap_make_request);

	vnswap_disk->major = vnswap_major;
	vnswap_disk->first_minor = 0;
	vnswap_disk->fops = &vnswap_fops;
	vnswap_disk->private_data = NULL;
	snprintf(vnswap_disk->disk_name, 16, "vnswap%d", 0);

	/* Actual capacity set using sysfs (/sys/block/vnswap<id>/disksize) */
	set_capacity(vnswap_disk, 0);

	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(vnswap_disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(vnswap_disk->queue,
				     VNSWAP_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(vnswap_disk->queue, PAGE_SIZE);
	blk_queue_io_opt(vnswap_disk->queue, PAGE_SIZE);
	blk_queue_max_hw_sectors(vnswap_disk->queue, PAGE_SIZE / SECTOR_SIZE);

	add_disk(vnswap_disk);

	vnswap_disksize = 0;
	vnswap_initialized = 0;

	ret = sysfs_create_group(&disk_to_dev(vnswap_disk)->kobj,
				 &vnswap_disk_attr_group);
	if (ret < 0) {
		pr_err("%s %d: Error creating sysfs group\n",
		       __func__, __LINE__);
		goto out_free_queue;
	}

	/* vnswap devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, vnswap_disk->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, vnswap_disk->queue);

out:
	return ret;

out_free_queue:
	blk_cleanup_queue(vnswap_disk->queue);

out_put_disk:
	put_disk(vnswap_disk);

	return ret;
}

static void destroy_device(void)
{
	if (vnswap_disk)
		sysfs_remove_group(&disk_to_dev(vnswap_disk)->kobj,
				   &vnswap_disk_attr_group);

	if (vnswap_disk) {
		del_gendisk(vnswap_disk);
		put_disk(vnswap_disk);
	}

	if (vnswap_disk->queue)
		blk_cleanup_queue(vnswap_disk->queue);
}

static int __init vnswap_init(void)
{
	int ret;

	vnswap_major = register_blkdev(0, "vnswap");
	if (vnswap_major <= 0) {
		pr_err("%s %d: Unable to get major number\n",
		       __func__, __LINE__);
		ret = -EBUSY;
		goto out;
	}

	ret = create_device();
	if (ret) {
		pr_err("%s %d: Unable to create vnswap_device\n",
		       __func__, __LINE__);
		goto free_devices;
	}

	return 0;

free_devices:
	unregister_blkdev(vnswap_major, "vnswap");
out:
	return ret;
}

static void __exit vnswap_exit(void)
{
	destroy_device();

	unregister_blkdev(vnswap_major, "vnswap");

	if (swap_header_page)
		__free_page(swap_header_page);
}

module_init(vnswap_init);
module_exit(vnswap_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Park Ju Hyung <qkrwngud825@gmail.com>");
MODULE_DESCRIPTION("Virtual block swap device based on vnswap");
