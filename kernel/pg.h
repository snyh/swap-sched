#ifndef __PG__
#define __PG__

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/memcontrol.h>

struct page_group;

bool pg_has(struct page_group* g, u64 k);
void pg_inc(struct page_group* g, u64 k);
struct page_group* find_pg(struct mem_cgroup* mc);
u64 page_key(struct page *p);
bool pg_full(struct page_group* g);


typedef struct uicache_pool_key {
  unsigned type;
  pgoff_t offset;
} pool_key_t;

typedef struct uicache_pool_value {
  struct hlist_node list;
  pool_key_t key;
  u8 data[PAGE_SIZE];
} pool_val_t;

int uicache_pool_store(pool_key_t k, struct page *page);
int uicache_pool_load(pool_key_t k, struct page *page);
void uicache_pool_delete_all(pool_key_t k);
void uicache_pool_delete(pool_key_t k);

void exit_hook(void);
int init_hook(void);


void pool_init(void);
void exit_pool(void);

int init_proc(void);
void exit_proc(void);

bool stop_monitor(const char* mcg_id);
bool begin_monitor(const char* mcg_id, u16 capacity);

#endif
