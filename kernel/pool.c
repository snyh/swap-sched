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

static atomic_t _uicache_stored_page_ = ATOMIC_INIT(0);

int uicache_stored_page()
{
  return atomic_read(&_uicache_stored_page_);
}


DEFINE_HASHTABLE(uicache_hash, sizeof(u64));

static DEFINE_SPINLOCK(_uicache_pool_lock);

inline u64 _pool_key_hash(pool_key_t k)
{
  return hash_64(k.offset, sizeof(u64));
}


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
  assert_spin_locked(&_uicache_pool_lock);

  hash_for_each_possible(uicache_hash, i, list, _pool_key_hash(k)) {
    if ((k.offset == i->key.offset) && (i->key.type == k.type)) {
      return i;
    }
  }
  return NULL;
}

static pool_val_t* _uicache_pool_new(pool_key_t k)
{
  pool_val_t *v;
  assert_spin_locked(&_uicache_pool_lock);

  v = kmem_cache_alloc(_mem_pool, GFP_ATOMIC);
  if (!v) {
    return NULL;
  }

  v->key = k;
  atomic_inc(&_uicache_stored_page_);

  hash_add(uicache_hash, &(v->list), _pool_key_hash(k));
  return v;
}

static void _uicache_pool_delete(pool_val_t* i)
{
  assert_spin_locked(&_uicache_pool_lock);
  hash_del(&(i->list));
  kmem_cache_free(_mem_pool, i);
  atomic_dec(&_uicache_stored_page_);
}

int uicache_pool_store(pool_key_t k, struct page *page)
{
  pool_val_t *i;
  u8 *src;
  if (atomic_read(&_uicache_stored_page_) > MAX_PAGES) {
    return -ENOSPC;
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

  if (page->mem_cgroup) {
    printk("uicache pool load %d %ld\n", k.type, k.offset);
  }
  return 0;
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
