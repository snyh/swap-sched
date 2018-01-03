#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/cpuhotplug.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/frontswap.h>
#include <linux/rbtree.h>
#include <linux/swap.h>
#include <linux/crypto.h>
#include <linux/mempool.h>
#include <linux/zpool.h>

#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/swapops.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>

static int uiswap_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
  printk("uiswap try saving %d %ld %p\n", type, offset, page);
  return -ENOMEM;
}

static int uiswap_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page)
{
  printk("uiswap try loading %d %ld %p\n", type, offset, page);
  return -1;
}

static void uiswap_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
  printk("uiswap try invalidating page %d %ld\n", type, offset);
  return;
}

static void uiswap_frontswap_invalidate_area(unsigned type)
{
  printk("uiswap try invalidating area of %d\n", type);
  return;
}

static void uiswap_frontswap_init(unsigned type)
{
  printk("init uiswap.");
  return;
}

static struct frontswap_ops uiswap_frontswap_ops = {
    .store = uiswap_frontswap_store,
    .load = uiswap_frontswap_load,
    .invalidate_page = uiswap_frontswap_invalidate_page,
    .invalidate_area = uiswap_frontswap_invalidate_area,
    .init = uiswap_frontswap_init
};

static int uiswap_init(void)
{
    frontswap_register_ops(&uiswap_frontswap_ops);
    return 0;
}
static void uiswap_exit(void)
{
}

module_init(uiswap_init);
module_exit(uiswap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("Cache UI reacting relevant pages");
