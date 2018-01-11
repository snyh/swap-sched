#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>

#include "pg.h"

#define INIT_PAGE_COUNT_VALUE  10
#define MAX_CGROUP_PATH_SIZE 128


u64 page_key(struct page *p)
{
  return (u64)page_to_pfn(p);
}

struct page_kv_counts {
  struct list_head list;
  u64 k;
  unsigned short v;
};

struct page_group {
  struct list_head list; //lisf of all_pg;

  struct mutex lock;

  struct list_head pages; //list head of page_kv_counts;

  // 目前cgroup1无法成功通过一个path来构建一个mem_cgroup, 因此只能存完整路径供后面比较.
  // cgroup_get_from_path 这个函数似乎只对cgroup2生效．
  char mcg_id[MAX_CGROUP_PATH_SIZE];

  u16 capacity;
  u16 count;
  u16 count_record;
};

LIST_HEAD(all_pg);

int _sort_by_ascending(void* prv,
                       struct list_head *a,
                       struct list_head *b)
{
  return list_entry(a, struct page_kv_counts, list)->v
    > list_entry(b, struct page_kv_counts, list)->v;
}

bool _pg_done(struct page_group* g)
{
  return g->count_record >= g->capacity;
}

bool _pg_full(struct page_group* g)
{
  BUG_ON(g->capacity == 0);
  return g->count >= g->capacity;
}

const char* _parse_mcg_id(struct mem_cgroup* mc)
{
  struct cgroup *cg;
  static char buf[MAX_CGROUP_PATH_SIZE] = {};

  BUG_ON(!mc);

  cg = mc->css.cgroup;
  cgroup_path(cg, buf, sizeof(buf));
  return buf;
}

struct page_group* _find_pg(const char* id)
{
  struct page_group *g;
  list_for_each_entry(g, &all_pg, list) {
    if (0 == strncmp(g->mcg_id, id, MAX_CGROUP_PATH_SIZE)) {
      return g;
    }
  }
  return NULL;
}

struct page_group* find_pg(struct mem_cgroup* mc)
{
  return _find_pg(_parse_mcg_id(mc));
}

bool pg_has(struct page_group* g, u64 k)
{
  struct page_kv_counts *i;
  mutex_lock(&(g->lock));
  list_for_each_entry(i, &(g->pages), list) {
    if (i->k == k) {
      mutex_unlock(&(g->lock));
      return true;
    }
  };
  mutex_unlock(&(g->lock));
  return false;
}

void pg_inc(struct page_group* g, u64 k)
{
  struct page_kv_counts *i, *tmp, *next;
  if (!g) {
    return;
  }

  if (_pg_done(g)) {
    return;
  } else {
    g->count_record++;
  }

  mutex_lock(&(g->lock));

  list_for_each_entry_safe(i, tmp, &(g->pages), list) {
    if (i->k == k) {
      i->v++;
      next = list_entry(i->list.next, struct page_kv_counts, list);
      // ensure the list's value is ascending order
      mutex_unlock(&(g->lock));
      return;
    }
  };

  if (_pg_full(g)) {
    list_sort(0, &(g->pages), _sort_by_ascending);
    i = list_first_entry(&(g->pages), struct page_kv_counts, list);
    i->k = k;
    i->v = INIT_PAGE_COUNT_VALUE;
  } else {
    i = kmalloc(sizeof(struct page_kv_counts), GFP_KERNEL);
    i->k = k;
    i->v = INIT_PAGE_COUNT_VALUE;
    list_add(&(i->list), &(g->pages));
    g->count++;
  }
  mutex_unlock(&(g->lock));
}

void pg_reduce(struct page_group *g)
{
  struct page_kv_counts *i, *tmp;
  mutex_lock(&(g->lock));
  list_for_each_entry_safe(i, tmp, &(g->pages), list) {
    if (i->v > 0) {
      i->v--;
    }
  };
  printk("Reduce Page Group %s\n", g->mcg_id);
  mutex_unlock(&(g->lock));
}

bool begin_monitor(const char* mcg_id, u16 capacity)
{
  struct page_group *g;
  g = _find_pg(mcg_id);

  if (!g) {
    g = kmalloc(sizeof(*g), GFP_KERNEL);
    INIT_LIST_HEAD(&(g->pages));
    mutex_init(&g->lock);
    memcpy(g->mcg_id, mcg_id, sizeof(g->mcg_id));
    g->count = 0;
    list_add(&(g->list), &all_pg);
  } else {
    pg_reduce(g);
  }
  g->capacity = capacity;
  g->count_record = 0;
  printk("uicache monitor %s %d\n", mcg_id, capacity);
  return true;
}

static void _destroy_page_group(struct page_group* g)
{
  struct page_kv_counts *i, *t;

  BUG_ON(!g);

  list_del(&(g->list));

  list_for_each_entry_safe(i, t, &(g->pages), list) {
    list_del(&(i->list));
    kfree(i);
  }

  kfree(g);
}

void _destroy_all_monitor(void)
{
  struct page_group *g, *t;
  list_for_each_entry_safe(g, t, &all_pg, list) {
    _destroy_page_group(g);
  }
}

// ----- DUMP Page Group information -----------
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

static int show_proc_content(struct seq_file *filp, void *p)
{
  struct page_group *g;
  struct page_kv_counts *i;
  int pos = 0;
  list_for_each_entry(g, &all_pg, list) {
    pos = 0;
    seq_printf(filp, "%s(%p) Count:%d Stored:%d Record:%d\n", g->mcg_id, g, g->count,
               uicache_stored_page(),
               g->count_record
               );
    list_for_each_entry(i, &(g->pages), list) {
      if (pos % 5 == 0) {
        seq_putc(filp, '\n');
      }
      pos++;
      seq_printf(filp, "0x%llx: %d ", i->k, i->v);
    }
    seq_putc(filp, '\n');
  }
  return 0;
}

static int proc_open_callback(struct inode *inode, struct file *filp)
{
  return single_open(filp, show_proc_content, 0);
}

static ssize_t trigger_monitor(struct file* f, const char __user* buf, size_t size, loff_t* pos)
{
  static char pbuf[MAX_CGROUP_PATH_SIZE] = {};
  if (size > MAX_CGROUP_PATH_SIZE) {
    return -1;
  }
  if (copy_from_user(pbuf, buf, size)) {
    return -EFAULT;
  }

  begin_monitor(strim(pbuf), 2500);
  return size;
}

static const struct file_operations proc_file_fops = {
  .owner = THIS_MODULE,
  .open = proc_open_callback,
  .read	= seq_read,
  .llseek = seq_lseek,
  .release = single_release,
  .write = trigger_monitor,
};

#define PROC_NAME "uicache"

int init_pg(void)
{
  struct proc_dir_entry *proc_file_entry = proc_create(PROC_NAME, 0, NULL, &proc_file_fops);
  printk("uicache proc %p\n", proc_file_entry);

  if (proc_file_entry == NULL)
    return -ENOMEM;
  return 0;
}
void exit_pg(void)
{
  remove_proc_entry(PROC_NAME, NULL);
  _destroy_all_monitor();
}
