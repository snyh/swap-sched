#include <linux/mm_types.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

#define MAX_PAGES (100 * 1024 * 1024 / PAGE_SIZE) // 最多使用100 MB 空间来存储page

static atomic_t _current_page_ = ATOMIC_INIT(0);

LIST_HEAD(uicache_pool);

static DEFINE_SPINLOCK(_uicache_pool_lock);

typedef struct uicache_pool_key {
  unsigned type;
  pgoff_t offset;
} pool_key_t;

typedef struct uicache_pool_value {
  struct list_head list;
  pool_key_t key;
  char data[PAGE_SIZE];
} pool_val_t;

inline static bool _key_equal(pool_key_t a, pool_key_t b)
{
  return (a.type == b.type) && (a.offset == b.offset);
}

pool_val_t* uicache_pool_find(pool_key_t k)
{
  pool_val_t *i;
  spin_lock(&_uicache_pool_lock);
  list_for_each_entry(i, &uicache_pool, list)  {
    if (_key_equal(i->key, k)) {
      spin_unlock(&_uicache_pool_lock);
      return i;
    }
  }
  spin_unlock(&_uicache_pool_lock);
  return NULL;
}

static pool_val_t* _uicache_pool_new(pool_key_t k)
{
  pool_val_t *v;
  v = kmalloc(sizeof(*v), GFP_ATOMIC);
  if (!v) {
    return NULL;
  }

  v->key = k;
  atomic_inc(&_current_page_);

  spin_lock(&_uicache_pool_lock);
  list_add(&(v->list), &uicache_pool);
  spin_unlock(&_uicache_pool_lock);
  return v;
}

int uicache_pool_store(pool_key_t k, struct page *page)
{
  pool_val_t *i;
  if (atomic_read(&_current_page_) > MAX_PAGES) {
    return -ENOMEM;
  }

  i = uicache_pool_find(k);
  if (!i) {
    i = _uicache_pool_new(k);
    if (!i) {
      return -ENOMEM;
    }
  }
  spin_lock(&_uicache_pool_lock);
  memcpy(i->data, kmap_atomic(page), PAGE_SIZE);
  spin_unlock(&_uicache_pool_lock);
  return 0;
}

int uicache_pool_load(pool_key_t k, struct page *page)
{
  pool_val_t *i;
  i = uicache_pool_find(k);
  if (!i) {
    return -1;
  }
  spin_lock(&_uicache_pool_lock);
  memcpy(kmap_atomic(page), i->data, PAGE_SIZE);
  spin_unlock(&_uicache_pool_lock);
  // TODO: add priority of this page in pg
  return 0;
}

static void _uicache_pool_delete(pool_val_t* i)
{
  list_del(&(i->list));
  kfree(i);
  atomic_dec(&_current_page_);
}

static void uicache_pool_delete(pool_key_t k)
{
  pool_val_t *i;
  i = uicache_pool_find(k);
  if (!i) {
    return;
  }
  spin_lock(&_uicache_pool_lock);
  _uicache_pool_delete(i);
  spin_unlock(&_uicache_pool_lock);
}

void uicache_pool_delete_all(pool_key_t k)
{
  pool_val_t *i, *t;
  spin_lock(&_uicache_pool_lock);
  list_for_each_entry_safe(i, t, &uicache_pool, list) {
    _uicache_pool_delete(i);
  }
  spin_unlock(&_uicache_pool_lock);
}
