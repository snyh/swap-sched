#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/slab.h>

#include "pg.h"

struct kmem_cache *_mem_pool;

#define MAX_PAGES (100 * 1024 * 1024 / PAGE_SIZE) // 最多使用100 MB 空间来存储page

static atomic_t _current_page_ = ATOMIC_INIT(0);

#define HASHSIZE sizeof(u64)

DEFINE_HASHTABLE(uicache_hash, HASHSIZE);

static DEFINE_SPINLOCK(_uicache_pool_lock);

void pool_init(void)
{
  _mem_pool = kmem_cache_create("uicache",
                                sizeof(struct uicache_pool_value),
                                0, SLAB_PANIC,
                                NULL);
}
void exit_pool(void)
{
  kmem_cache_destroy(_mem_pool);
}

static pool_val_t* _uicache_pool_find(pool_key_t k)
{
  pool_val_t *i;

  hash_for_each_possible(uicache_hash, i, list, k.offset) {
    if (i->key.type == k.type) {
      return i;
    }
  }
  return NULL;
}

static pool_val_t* _uicache_pool_new(pool_key_t k)
{
  pool_val_t *v;
  v = kmem_cache_alloc(_mem_pool, GFP_ATOMIC);
  if (!v) {
    return NULL;
  }

  v->key = k;
  atomic_inc(&_current_page_);

  hash_add(uicache_hash, &(v->list), k.offset);
  return v;
}

int uicache_pool_store(pool_key_t k, struct page *page)
{
  pool_val_t *i;
  u8 *src;
  if (atomic_read(&_current_page_) > MAX_PAGES) {
    return -ENOMEM;
  }

  spin_lock(&_uicache_pool_lock);
  i = _uicache_pool_find(k);
  if (!i) {
    i = _uicache_pool_new(k);
    if (!i) {
      spin_unlock(&_uicache_pool_lock);
      return -ENOMEM;
    }
  }

  src = kmap_atomic(page);
  memcpy(i->data, src, PAGE_SIZE);
  kunmap_atomic(src);
  spin_unlock(&_uicache_pool_lock);
  return 0;
}

int uicache_pool_load(pool_key_t k, struct page *page)
{
  pool_val_t *i;
  u8 *dst;

  spin_lock(&_uicache_pool_lock);
  i = _uicache_pool_find(k);
  if (!i) {
    spin_unlock(&_uicache_pool_lock);
    printk("not found... %ld\n", k.offset);
    return -1;
  }
  dst = kmap_atomic(page);
  memcpy(dst, i->data, PAGE_SIZE);
  kunmap_atomic(dst);
  spin_unlock(&_uicache_pool_lock);

  //  WARN_ON(!page->mem_cgroup);

  if (page->mem_cgroup) {
    printk("uicache pool load %d %ld\n", k.type, k.offset);
    pg_inc(find_pg(page->mem_cgroup), page_key(page));
  }
  return 0;
}

static void _uicache_pool_delete(pool_val_t* i)
{
  hash_del(&(i->list));
  kmem_cache_free(_mem_pool, i);
  atomic_dec(&_current_page_);
}

void uicache_pool_delete(pool_key_t k)
{
  pool_val_t *i;

  spin_lock(&_uicache_pool_lock);
  i = _uicache_pool_find(k);
  if (!i) {
    spin_unlock(&_uicache_pool_lock);
    return;
  }
  _uicache_pool_delete(i);
  spin_unlock(&_uicache_pool_lock);
}

void uicache_pool_delete_all(pool_key_t k)
{
  struct hlist_node *t;
  pool_val_t *i;
  int bkt;

  spin_lock(&_uicache_pool_lock);
  hash_for_each_safe(uicache_hash, bkt, t, i, list) {
    _uicache_pool_delete(i);
  }
  spin_unlock(&_uicache_pool_lock);
}
