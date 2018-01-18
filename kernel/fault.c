#include <linux/stringhash.h>
#include <linux/hashtable.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/radix-tree.h>
#include <linux/workqueue.h>
#include <linux/swap.h>

DEFINE_HASHTABLE(atoms, 8);

#define NAME_SIZE 128

struct record_task {
  pid_t pid;
  char name[NAME_SIZE];
  unsigned long addr;
  bool is_anon;
  bool is_release;
  struct work_struct w;
};

struct kv {
  struct list_head list;
  unsigned long addr;
  u32 count;
};

#define MAX_COUNT_SIZE 256
struct atom {
  struct hlist_node hlist;
  pid_t pid;
  char name[NAME_SIZE];
  u32 count_sum[MAX_COUNT_SIZE+1];
  struct list_head anon_detail;
  struct list_head file_detail;
};

DEFINE_MUTEX(record_lock);

struct readswapin_args {
  unsigned long address;
  bool *miss_from_swapcache;
};

static void do_record_fault(struct record_task *t);
static void do_release(pid_t pid);
void _destroy_atom(struct atom* i);

static void work_func(struct work_struct *work)
{
  struct record_task *t = container_of(work, struct record_task, w);
  if (t->is_release) {
    do_release(t->pid);
  } else {
    do_record_fault(t);
  }
  kfree(t);
}

void prepare_release(pid_t pid)
{
  struct record_task *i = 0;
  i = kmalloc(sizeof(*i), GFP_ATOMIC);
  if (unlikely(!i))
    return;
  i->pid = pid;
  i->is_release = true;
  INIT_WORK(&(i->w), work_func);

  schedule_work(&(i->w));
}

void prepare_record_fault(unsigned long addr, bool is_anon)
{
  struct record_task *i = 0;
  int name_s;
  i = kzalloc(sizeof(*i), GFP_ATOMIC);
  if (unlikely(!i))
    return;

  i->is_release = false;
  i->pid = current->pid;
  name_s = strlen(current->comm);
  memcpy(i->name, current->comm, min(NAME_SIZE, name_s));
  i->addr = addr;
  i->is_anon = is_anon;
  INIT_WORK(&(i->w), work_func);
  schedule_work(&(i->w));
}

void _atom_count_sum(struct atom * i, int c)
{
  if (c >= MAX_COUNT_SIZE) {
    i->count_sum[MAX_COUNT_SIZE]++;
  } else if (c == 1) {
    i->count_sum[1]++;
  } else {
    i->count_sum[c]++;
    i->count_sum[c-1]--;
  }
}

void _record_detail(struct atom* i, unsigned long addr, bool is_anon)
{
  struct kv* p;
  struct list_head *head;

  if (is_anon) {
    head = &(i->anon_detail);
  } else {
    head = &(i->file_detail);
  }

  list_for_each_entry(p, head, list) {
    if (p->addr == addr) {
      p->count++;
      _atom_count_sum(i, p->count);
      return;
    }
  }

  p = kmalloc(sizeof(*p), GFP_ATOMIC);
  if (unlikely(!p))
    return;
  p->addr = addr;
  p->count = 1;
  _atom_count_sum(i, p->count);
  list_add(&(p->list), head);
}

static void do_release(pid_t pid)
{
  struct atom *i = NULL;
  mutex_lock(&record_lock);
  hash_for_each_possible(atoms, i, hlist, pid) {
    if (i->pid == pid)
      break;
  }
  if (i) {
    _destroy_atom(i);
  }
  mutex_unlock(&record_lock);
}
static void do_record_fault(struct record_task *t)
{
  struct atom *i = NULL;

  mutex_lock(&record_lock);
  hash_for_each_possible(atoms, i, hlist, t->pid) {
    if (i->pid == t->pid) {
      _record_detail(i, t->addr, t->is_anon);
      mutex_unlock(&record_lock);
      return;
    }
  }
  mutex_unlock(&record_lock);

  i = kzalloc(sizeof(*i), GFP_ATOMIC);
  if (unlikely(!i))
    return;

  INIT_LIST_HEAD(&(i->anon_detail));
  INIT_LIST_HEAD(&(i->file_detail));
  i->pid = t->pid;
  memcpy(i->name, t->name, NAME_SIZE);

  mutex_lock(&record_lock);
  _record_detail(i, t->addr, t->is_anon);
  hash_add(atoms, &(i->hlist), t->pid);
  mutex_unlock(&record_lock);
}

void _record_dump_detail(struct seq_file* file, struct atom *i)
{
  struct kv *p;

  int greater_than_one = 0, pos = 2;
  for (; pos < MAX_COUNT_SIZE; pos++) {
    greater_than_one += i->count_sum[pos];
  }
  if (greater_than_one == 0) {
    return;
  }
  seq_printf(file, "%d(%s)", i->pid, i->name);

  pos = 0;
  list_for_each_entry(p, &(i->anon_detail), list) {
    pos += p->count;
  }
  seq_printf(file, "\tAnon:%d", pos);

  pos = 0;

  list_for_each_entry(p, &(i->file_detail), list) {
    pos += p->count;
  }
  seq_printf(file, "\tFile:%d\n", pos);

  seq_putc(file, '\t');
  for (pos = 0; pos < MAX_COUNT_SIZE; pos++) {
    if (i->count_sum[pos] > 0)
      seq_printf(file, " [%d:%d]", pos+1, i->count_sum[pos]);
  }
  seq_putc(file, '\n');
}

void record_dump(struct seq_file* file)
{
  struct atom *i;
  struct hlist_node *tmp;
  int bkt;
  mutex_lock(&record_lock);
  hash_for_each_safe(atoms, bkt, tmp, i, hlist) {
    if (current->pid == i->pid) {
      continue;
    }
    _record_dump_detail(file, i);
  }
  mutex_unlock(&record_lock);
}

int hook_entry_swapin_fault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  struct readswapin_args *d;
  if (!current->mm)
    return 1;
  d = (struct readswapin_args*)ri->data;
  d->address = (unsigned long)regs->cx;
  d->miss_from_swapcache = (void*)(regs->r8);
  return 0;
}

int hook_ret_swapin_fault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  int __maybe_unused retval = regs_return_value(regs);
  struct readswapin_args *d = (struct readswapin_args*)ri->data;
  bool *miss = d->miss_from_swapcache;
  if (miss && *miss) {
    prepare_record_fault(d->address, true);
  }
  return 0;
}

int hook_filemap_fault(struct kprobe *p, struct pt_regs *regs)
{
  struct vm_fault *vmf = (void*)(regs->si);
  unsigned long addr = (unsigned long)vmf->virtual_address;
  if (!current->mm) {
    return 1;
  }
  prepare_record_fault(addr, false);
  return 0;
}

int hook_do_exit(struct kprobe *p, struct pt_regs *regs)
{
  if (!current->mm) {
    return 1;
  }
  prepare_release(current->pid);
  return 0;
}

#define NUM_PROBE 2
static struct kprobe pp[NUM_PROBE] = {
  {
    .symbol_name = "do_exit",
    .pre_handler = hook_do_exit,
  },
  {
    .symbol_name = "filemap_fault",
    .pre_handler = hook_filemap_fault,
  },
};

#define NUM_RETPROBE 1
static struct kretprobe retpp[NUM_RETPROBE] = {
  {
    .handler = hook_ret_swapin_fault,
    .entry_handler = hook_entry_swapin_fault,
    .data_size = sizeof(struct readswapin_args),
    .kp.symbol_name = "__read_swap_cache_async",
  },
};

int register_pp(void)
{
  int ret = 0;
  int i, j;
  for (i=0; i<NUM_PROBE;i++) {
    ret = register_kprobe(&(pp[i]));
    if (ret) {
      for (j=i; j>=0; j--) {
        unregister_kprobe(&(pp[j]));
      }
      return ret;
    }
  }
  for (i=0; i<NUM_RETPROBE;i++) {
    ret = register_kretprobe(&(retpp[i]));
    if (ret) {
      for (j=i; j>=0; j--) {
        unregister_kretprobe(&(retpp[j]));
      }
      return ret;
    }
  }
  return 0;
}

void unregister_pp(void)
{
  int i;
  for (i=0; i < NUM_RETPROBE; i++) {
    unregister_kretprobe(&retpp[i]);
  }
  for (i=0; i < NUM_PROBE; i++) {
    unregister_kprobe(&pp[i]);
  }
}

#define PROC_NAME "fault"

static int show_proc_content(struct seq_file *filp, void *p)
{
  record_dump(filp);
  return 0;
}

static int proc_open_callback(struct inode *inode, struct file *filp)
{
  return single_open(filp, show_proc_content, 0);
}

static const struct file_operations proc_file_fops = {
  .owner = THIS_MODULE,
  .open = proc_open_callback,
  .read	= seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

int fault_record_init(void)
{
  int ret = -1;
  struct proc_dir_entry *proc_file_entry;

  proc_file_entry = proc_create(PROC_NAME, 0, NULL, &proc_file_fops);
  if (proc_file_entry == NULL)
    return -ENOMEM;

  ret = register_pp();
  if (ret < 0) {
    remove_proc_entry(PROC_NAME, NULL);
    printk(KERN_INFO "register_kprobe failed, returned %d\n",
           ret);
    return -1;
  }
  return 0;
}

void _destroy_atom(struct atom* i)
{
  struct kv *p, *t;
  list_for_each_entry_safe(p, t, &(i->anon_detail), list) {
    list_del(&(p->list));
    kfree(p);
  }
  list_for_each_entry_safe(p, t, &(i->file_detail), list) {
    list_del(&(p->list));
    kfree(p);
  }
  hash_del(&(i->hlist));
  kfree(i);
}

void fault_record_exit(void)
{
  struct atom *i;
  struct hlist_node *t;
  int bkt;

  unregister_pp();

  flush_workqueue(system_wq);

  remove_proc_entry(PROC_NAME, NULL);

  mutex_lock(&record_lock);
  hash_for_each_safe(atoms, bkt, t, i, hlist) {
    _destroy_atom(i);
  }
  mutex_unlock(&record_lock);
}

module_init(fault_record_init);
module_exit(fault_record_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("recording faults about pagecache and swapcache");
