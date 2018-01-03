#include <linux/module.h>
#include <linux/frontswap.h>
#include <linux/cleancache.h>
#include <linux/pagemap.h>
#include <linux/memcontrol.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define _fmt(fmt) KERN_ERR""KBUILD_MODNAME ": " fmt

#define MAX_COUNT 100

struct data_item {
  struct list_head list;
  pgoff_t index;
  char data[PAGE_SIZE];
};

struct uicache_pool {
  unsigned short mid;
  struct list_head items;
  u32 count;
  spinlock_t lock;
};

static struct uicache_pool *_p_ = NULL;

static struct uicache_pool* create_uicache_pool(unsigned short mid)
{
  struct uicache_pool *pool = 0;
  pool = kzalloc(sizeof(*pool), GFP_KERNEL);
  pool->items.next = &(pool->items);
  pool->items.prev = &(pool->items);
  pool->mid = mid;
  spin_lock_init(&pool->lock);
  return pool;
}

static struct uicache_pool* find_pool(struct page* page)
{
  return _p_;
}

static void destroy_data_item(struct data_item *i)
{
  kfree(i);
}
static struct data_item* create_data_item(pgoff_t offset, void* d)
{
  struct data_item* i = 0;
  i = kzalloc(sizeof(*i), GFP_ATOMIC);
  BUG_ON(!i);
  i->index = offset;
  INIT_LIST_HEAD(&i->list);
  memcpy(&(i->data), d, PAGE_SIZE);
  return i;
}

int uicache_pool_insert(struct uicache_pool *p, pgoff_t offset, struct page *page)
{
  struct data_item *d = NULL, *i = NULL;

  BUG_ON(!p);

  spin_lock(&p->lock);
  if (p->count >= MAX_COUNT) {
    spin_unlock(&p->lock);
    return -1;
  }

  list_for_each_entry(d, &p->items, list) {
    if (d->index == offset) {
      spin_unlock(&p->lock);
      return 0;
    }
  }

  void * s = kmap_atomic(page);
  i = create_data_item(offset, s);
  list_add(&p->items, &(i->list));
  p->count++;
  spin_unlock(&p->lock);
  return 0;
}

int uicache_pool_get(struct uicache_pool *p, pgoff_t offset, struct page *page)
{
  struct data_item *d = NULL;

  BUG_ON(!p);

  spin_lock(&p->lock);

  list_for_each_entry(d, &p->items, list) {
    if (d->index == offset) {
      printk(_fmt("uicache fetch %ld\n"), offset);
      memcpy(kmap_atomic(page), &(d->data), PAGE_SIZE);
      spin_unlock(&p->lock);
      return 0;
    }
  }
  spin_unlock(&p->lock);
  return -1;
}

int uicache_pool_delete(struct uicache_pool *p, pgoff_t offset)
{
  struct data_item *d = NULL, *i=0;

  BUG_ON(!p);

  spin_lock(&p->lock);

  list_for_each_entry_safe(d, i, &p->items, list) {
    if (d->index == offset) {
      printk(_fmt("uicache delete %ld\n"), offset);
      list_del(&d->list);
      destroy_data_item(d);
      p->count--;
      spin_unlock(&p->lock);
      return 0;
    }
  }
  spin_unlock(&p->lock);
  return -1;
}

static int uicache_frontswap_store(unsigned type, pgoff_t offset,
				struct page *page)
{
  int ret = -1;
  struct uicache_pool *p = find_pool(page);
  if (!p) {
    return -1;
  }

  ret = uicache_pool_insert(p, offset, page);
  printk(_fmt("uicache_frontswap_store %p %d %ld %p RET:%d\n"), p, type, offset, page, ret);
  return ret;
}

static int uicache_frontswap_load(unsigned type, pgoff_t offset,
				struct page *page)
{
  struct uicache_pool *p = find_pool(page);
  if (!p) {
    return -1;
  }
  int ret = uicache_pool_get(p, offset, page);
  printk(_fmt("uicache_frontswap_load %p %d %ld %p RET:%d\n"), p, type, offset, page, ret);
  return ret;
}

static void uicache_frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
  struct uicache_pool *p = find_pool(0);
  if (!p) {
    return;
  }
  uicache_pool_delete(p, offset);
}

static void uicache_frontswap_invalidate_area(unsigned type)
{
  printk(_fmt("uicache try invalidating area of %d\n"), type);
  return;
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
  _p_ = create_uicache_pool(1);
  printk(_fmt("uicache_init %p\n"), _p_);
  frontswap_register_ops(&uicache_frontswap_ops);

  if (0) {
    cleancache_register_ops(&uicache_cleancache_ops);
  }
  return 0;
}
static void uicache_exit(void)
{
}

module_init(uicache_init);
module_exit(uicache_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("Cache UI reacting relevant pages");
