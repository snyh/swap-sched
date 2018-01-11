#include <linux/module.h>
#include <linux/frontswap.h>
#include <linux/cleancache.h>
#include <linux/pagemap.h>
#include <linux/memcontrol.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pg.h"

static DEFINE_SPINLOCK(_uicache_lock);

static int uicache_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
  struct page_group *pg;
  pool_key_t t;
  int ret = -1;

  spin_lock(&_uicache_lock);

  if (!page->mem_cgroup) {
    spin_unlock(&_uicache_lock);
    return -1;
  }

  pg = find_pg(page->mem_cgroup);
  if (!pg || !pg_has(pg, page_key(page))) {
    spin_unlock(&_uicache_lock);
    return -1;
  }

  t.type = type;
  t.offset = offset;

  ret = uicache_pool_store(t, page);
  spin_unlock(&_uicache_lock);
  return ret;
}

static int uicache_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page)
{
  int ret = -1;
  pool_key_t t = {
    .type = type,
    .offset = offset,
  };

  spin_lock(&_uicache_lock);
  ret = uicache_pool_load(t, page);
  spin_unlock(&_uicache_lock);
  return ret;
}

static void uicache_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
  pool_key_t t = {
    .type = type,
    .offset = offset,
  };
  spin_lock(&_uicache_lock);
  uicache_pool_delete(t);
  spin_unlock(&_uicache_lock);
}

static void uicache_frontswap_invalidate_area(unsigned type)
{
  pool_key_t t = {
    .type = type,
  };
  spin_lock(&_uicache_lock);
  uicache_pool_delete_all(t);
  spin_unlock(&_uicache_lock);
  printk("uicache try invalidating area of %d\n", type);
}

static void uicache_frontswap_init(unsigned type)
{
  printk("init uicache %d.", type);
  return;
}

static struct frontswap_ops uicache_frontswap_ops = {
    .store = uicache_frontswap_store,
    .load = uicache_frontswap_load,
    .invalidate_page = uicache_frontswap_invalidate_page,
    .invalidate_area = uicache_frontswap_invalidate_area,
    .init = uicache_frontswap_init,
};

static void uicache_cleancache_put_page(int pool, struct cleancache_filekey key,
                     pgoff_t index, struct page *page)
{
  printk("uicache cleancache_put_page\n");
}
static int uicache_cleancache_get_page(int pool, struct cleancache_filekey key,
                    pgoff_t index, struct page *page)
{
  printk("uicache cleancache_get_page\n");
  return -1;
}
static void uicache_cleancache_flush_page(int pool, struct cleancache_filekey key,
                       pgoff_t index)
{
  printk("uicache cleancache_flush_page\n");
}
static void uicache_cleancache_flush_inode(int pool, struct cleancache_filekey key)
{
  printk("uicache cleancache_flush_inode\n");
}
static void uicache_cleancache_flush_fs(int pool)
{
  printk("uicache cleancache_flush_fs\n");
}
static int uicache_cleancache_init_fs(size_t pagesize)
{
  printk("uicache cleancache init fs\n");
  return 0;
}
static int uicache_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
  printk("uicache cleancache init shared fs\n");
  return 0;
}

static struct cleancache_ops uicache_cleancache_ops = {
    .put_page = uicache_cleancache_put_page,
    .get_page = uicache_cleancache_get_page,
    .invalidate_page = uicache_cleancache_flush_page,
    .invalidate_inode = uicache_cleancache_flush_inode,
    .invalidate_fs = uicache_cleancache_flush_fs,
    .init_shared_fs = uicache_cleancache_init_shared_fs,
    .init_fs = uicache_cleancache_init_fs
};

static int uicache_init(void)
{
  int ret;
  printk("uicache_init1\n");

  pool_init();

  ret = init_hook();
  if (ret) {
    return ret;
  }

  ret = init_pg();
  if (ret) {
    return ret;
  }

  if (0) {
    cleancache_register_ops(&uicache_cleancache_ops);
    frontswap_register_ops(&uicache_frontswap_ops);
  }
  return 0;
}

static void uicache_exit(void)
{
  exit_pg();
  exit_hook();
  exit_pool();
}

module_init(uicache_init);
module_exit(uicache_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("Cache UI reacting relevant pages");
