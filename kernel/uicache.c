#include <linux/module.h>
#include <linux/frontswap.h>
#include <linux/cleancache.h>
#include <linux/pagemap.h>
#include <linux/memcontrol.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pg.h"

#include "pool.c"
#include "pg.c"
#include "hook.c"

#define _fmt(fmt) KERN_ERR""KBUILD_MODNAME ": " fmt

static int uicache_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
  struct page_group *pg;
  pool_key_t t;

  if (!page->mem_cgroup) {
    return -1;
  }

  pg = find_pg(page->mem_cgroup);
  if (!pg || !pg_has(pg, page_key(page))) {
    return -1;
  }

  t.type = type;
  t.offset = offset;

  return uicache_pool_store(t, page);
}

static int uicache_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page)
{
  int ret = -1;
  pool_key_t t = {
    .type = type,
    .offset = offset,
  };
  ret = uicache_pool_load(t, page);
  WARN_ON(ret!=0);

  return ret;
}

static void uicache_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
  pool_key_t t = {
    .type = type,
    .offset = offset,
  };
  uicache_pool_delete(t);
}

static void uicache_frontswap_invalidate_area(unsigned type)
{
  pool_key_t t = {
    .type = type,
  };
  uicache_pool_delete_all(t);
  printk(_fmt("uicache try invalidating area of %d\n"), type);
}

static void uicache_frontswap_init(unsigned type)
{
  printk(_fmt("init uicache %d."), type);
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
  printk(_fmt("uicache cleancache_put_page\n"));
}
static int uicache_cleancache_get_page(int pool, struct cleancache_filekey key,
                    pgoff_t index, struct page *page)
{
  printk(_fmt("uicache cleancache_get_page\n"));
  return -1;
}
static void uicache_cleancache_flush_page(int pool, struct cleancache_filekey key,
                       pgoff_t index)
{
  printk(_fmt("uicache cleancache_flush_page\n"));
}
static void uicache_cleancache_flush_inode(int pool, struct cleancache_filekey key)
{
  printk(_fmt("uicache cleancache_flush_inode\n"));
}
static void uicache_cleancache_flush_fs(int pool)
{
  printk(_fmt("uicache cleancache_flush_fs\n"));
}
static int uicache_cleancache_init_fs(size_t pagesize)
{
  printk(_fmt("uicache cleancache init fs\n"));
  return 0;
}
static int uicache_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
  printk(_fmt("uicache cleancache init shared fs\n"));
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
  printk(_fmt("uicache_init1\n"));

  if (0) {
    cleancache_register_ops(&uicache_cleancache_ops);
  }

  pool_init();

  ret = init_hook();
  if (ret) {
    return ret;
  }

  ret = init_proc();
  if (ret) {
    return ret;
  }

  begin_monitor("/333", 1000);
  frontswap_register_ops(&uicache_frontswap_ops);
  return 0;
}

static void uicache_exit(void)
{
  exit_proc();
  exit_hook();
  exit_pool();

  stop_monitor("/333");
}

module_init(uicache_init);
module_exit(uicache_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("Cache UI reacting relevant pages");
