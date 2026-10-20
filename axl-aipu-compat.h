/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2025 Axelera AI. All rights reserved.  */

/*
 * Kernel API compatibility shims.
 *
 * The driver targets kernels from 5.4 (Yocto dunfell) up to mainline, so APIs
 * introduced after 5.4 are wrapped here instead of being open-coded behind a
 * version guard at every call site. Wrappers keep an axl_ prefix rather than
 * #define-ing the upstream names, so that a distro kernel carrying a backport
 * (RHEL and friends) cannot collide with them.
 */
#ifndef __AXL_AIPU_COMPAT_H__
#define __AXL_AIPU_COMPAT_H__

#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static inline long axl_pin_user_pages(unsigned long start, unsigned long nr,
				      unsigned int gup_flags,
				      struct page **pages)
{
	return pin_user_pages_fast(start, nr, gup_flags, pages);
}

static inline void axl_unpin_user_pages(struct page **pages, unsigned long nr,
					bool make_dirty)
{
	unpin_user_pages_dirty_lock(pages, nr, make_dirty);
}
#else
static inline long axl_pin_user_pages(unsigned long start, unsigned long nr,
				      unsigned int gup_flags,
				      struct page **pages)
{
	return get_user_pages_fast(start, nr, gup_flags, pages);
}

static inline void axl_unpin_user_pages(struct page **pages, unsigned long nr,
					bool make_dirty)
{
	unsigned long i;

	for (i = 0; i < nr; i++) {
		if (make_dirty && !PageDirty(pages[i]))
			set_page_dirty_lock(pages[i]);
		put_page(pages[i]);
	}
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static inline int axl_dma_map_sgtable(struct device *dev, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      unsigned long attrs)
{
	return dma_map_sgtable(dev, sgt, dir, attrs);
}

static inline void axl_dma_unmap_sgtable(struct device *dev,
					 struct sg_table *sgt,
					 enum dma_data_direction dir,
					 unsigned long attrs)
{
	dma_unmap_sgtable(dev, sgt, dir, attrs);
}

#define axl_for_each_sgtable_dma_sg(sgt, sg, i) \
	for_each_sgtable_dma_sg(sgt, sg, i)
#else
static inline int axl_dma_map_sgtable(struct device *dev, struct sg_table *sgt,
				      enum dma_data_direction dir,
				      unsigned long attrs)
{
	int nents;

	nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	if (nents <= 0)
		return -EIO;

	sgt->nents = nents;
	return 0;
}

static inline void axl_dma_unmap_sgtable(struct device *dev,
					 struct sg_table *sgt,
					 enum dma_data_direction dir,
					 unsigned long attrs)
{
	dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
}

#define axl_for_each_sgtable_dma_sg(sgt, sg, i) \
	for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)
#endif

#endif // __AXL_AIPU_COMPAT_H__
