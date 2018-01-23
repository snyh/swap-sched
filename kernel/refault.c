#include <linux/stringhash.h>
#include <linux/hashtable.h>
#include <linux/mm.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/version.h>
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

struct refault_task {
  struct work_struct w;
  pid_t id;
  int ret;
  char name[NAME_SIZE+1];
  unsigned long address;
  struct file *file;
};

struct kv {
  struct list_head list;
  unsigned long addr;
  u32 count;
};

#define MAX_COUNT_SIZE 256
struct atom {
  struct hlist_node hlist;

  unsigned long id;
  char desc[NAME_SIZE+1];
  u32 count_sum[MAX_COUNT_SIZE+1];
  struct list_head detail;
};

DEFINE_MUTEX(record_lock);

static void do_record_refault(struct refault_task *t);
void _destroy_atom(struct atom* i);

unsigned short task_memcg_id(pid_t p)
{
  struct task_struct *t;
  struct mem_cgroup *cg = 0;
  unsigned short id = 0;

  rcu_read_lock();
  for_each_process(t) {
    if (t->pid != p) {
      continue;
    }
    cg = mem_cgroup_from_task(t);
    break;
  }
  if (cg) {
    id = mem_cgroup_id(cg);
  }
  rcu_read_unlock();
  return id;
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

void _record_detail(struct atom* i, unsigned long addr)
{
  struct kv* p;
  struct list_head *head = &(i->detail);

  list_for_each_entry(p, head, list) {
    if (p->addr == addr) {
      p->count++;
      _atom_count_sum(i, p->count);
      return;
    }
  }

  p = kzalloc(sizeof(*p), GFP_ATOMIC);
  if (unlikely(!p))
    return;
  p->addr = addr;
  p->count = 1;
  _atom_count_sum(i, p->count);
  list_add(&(p->list), head);
}

void _dump_block(struct seq_file* file, struct list_head* head, int min)
{
  struct kv *p;
  unsigned long all = 0;
  unsigned long good_num = 0;
  unsigned long good_sum = 0;
  list_for_each_entry(p, head, list) {
    all += p->count;
    if (p->count > min) {
      good_sum += p->count;
      good_num++;
    }
  }
  if (good_num == 0 || all == 0) {
    return;
  }
  seq_printf(file, "\tUse %ldKB reduce %luMB IO(%ld%%) ",
             good_num * 4 , good_sum * 4 / 1024, good_sum*100 / all);
  list_for_each_entry(p, head, list) {
    all += p->count;
    if (p->count > min) {
      seq_printf(file, " 0x%lx %u", p->addr, p->count);
    }
  }
}

void _record_dump_detail(struct seq_file* file, struct atom *i)
{
  struct kv *p;
  unsigned long all = 0;

  list_for_each_entry(p, &(i->detail), list) {
    all += p->count;
  }
  seq_printf(file, "%ld\t%ld\t%s", all, i->id, i->desc);

  _dump_block(file, &(i->detail), 3);
  seq_putc(file, '\n');
}

void record_dump(struct seq_file* file)
{
  struct atom *i;
  struct hlist_node *tmp;
  int bkt;

  seq_printf(file, "REFAULTS\tID\tDESC\n");

  mutex_lock(&record_lock);
  hash_for_each_safe(atoms, bkt, tmp, i, hlist) {
    int pos, greater_than_one = 0;
    for (pos=2; pos < MAX_COUNT_SIZE; pos++) {
      greater_than_one = i->count_sum[pos];
      if (greater_than_one > 0) {
        _record_dump_detail(file, i);
        break;
      }
    }
  }
  mutex_unlock(&record_lock);
}


int hook_entry_do_swap_page(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  struct refault_task *d;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0))
  struct vm_fault *vmf = (void*)(regs->di);
  unsigned long addr = (unsigned long)(vmf->address);
#else
  struct fault_env *env = (void*)(regs->di);
  unsigned long addr = env->address;
#endif
  if (!current->mm)
    return 1;

  BUG_ON(!addr);
  d = (struct refault_task*)ri->data;
  d->address = addr;
  d->file = 0;
  d->id = ri->task->pid;
  d->name[0] = '*';
  strncpy(d->name+1, current->comm, NAME_SIZE-1);
  return 0;
}

int hook_entry_file_refault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  struct refault_task *d;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0))
  struct vm_fault *vmf = (void*)(regs->di);
  struct vm_area_struct* vma = vmf->vma;
  unsigned long addr = vmf->pgoff;
#else
  struct vm_area_struct* vma = (void*)(regs->di);
  struct vm_fault *vmf = (void*)(regs->si);
  unsigned long addr = vmf->pgoff;

#endif
  BUG_ON(!vma->vm_file);
  d = (struct refault_task*)ri->data;
  d->address = addr;
  d->file = get_file(vma->vm_file);
  d->id = file_inode(vma->vm_file)->i_ino;
  return 0;
}

static void do_record_refault(struct refault_task *t)
{
  struct atom *i = NULL;

  mutex_lock(&record_lock);
  hash_for_each_possible(atoms, i, hlist, t->id) {
    if (i->id == t->id) {
      _record_detail(i, t->address);
      mutex_unlock(&record_lock);
      return;
    }
  }
  mutex_unlock(&record_lock);

  i = kzalloc(sizeof(*i), GFP_ATOMIC);
  if (unlikely(!i))
    return;

  INIT_LIST_HEAD(&(i->detail));
  i->id = t->id;
  strncpy(i->desc, t->name, NAME_SIZE);

  mutex_lock(&record_lock);
  _record_detail(i, t->address);
  hash_add(atoms, &(i->hlist), t->id);
  mutex_unlock(&record_lock);
}


static void work_func(struct work_struct *work)
{
  struct refault_task *t = container_of(work, struct refault_task, w);
  static char buf[NAME_SIZE+1] = {};
  char* tmp = 0;
  if (t->ret & VM_FAULT_MAJOR) {
    if (t->file) {
      tmp = file_path(t->file, buf, NAME_SIZE);
      if (IS_ERR(tmp)) {
        strncpy(t->name, "tooloong", NAME_SIZE);
      } else {
        strncpy(t->name, tmp, NAME_SIZE);
      }
      do_record_refault(t);
    } else {
      do_record_refault(t);
    }
  }
  if (t->file) {
    fput(t->file);
    t->file = 0;
  }
  kfree(t);
}

int hook_ret_refault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  struct refault_task *d = kmemdup(ri->data, sizeof(struct refault_task), GFP_ATOMIC);
  d->ret = regs_return_value(regs);
  INIT_WORK(&(d->w), work_func);
  schedule_work(&(d->w));
  return 0;
}

static struct kretprobe krp1 = {
  .kp.symbol_name = "shmem_fault",
  .handler = hook_ret_refault,
  .data_size = sizeof(struct refault_task),
  .entry_handler = hook_entry_file_refault,
};
static struct kretprobe krp2 = {
  .kp.symbol_name = "filemap_fault",
  .handler = hook_ret_refault,
  .data_size = sizeof(struct refault_task),
  .entry_handler = hook_entry_file_refault,
};
static struct kretprobe krp3 = {
  .handler = hook_ret_refault,
  .entry_handler = hook_entry_do_swap_page,
  .data_size = sizeof(struct refault_task),
  .kp.symbol_name = "do_swap_page",
};

#define NUM_RETPROBE 3
static struct kretprobe *retpp[NUM_RETPROBE] = { &krp1, &krp2, &krp3 };

int register_hooks(void)
{
  return register_kretprobes(retpp, NUM_RETPROBE);
}

void unregister_hooks(void)
{
  unregister_kretprobes(retpp, NUM_RETPROBE);
}

#define PROC_NAME "refault"

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

int refault_record_init(void)
{
  int ret = -1;
  struct proc_dir_entry *proc_file_entry;

  proc_file_entry = proc_create(PROC_NAME, 0, NULL, &proc_file_fops);
  if (proc_file_entry == NULL)
    return -ENOMEM;

  ret = register_hooks();
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
  list_for_each_entry_safe(p, t, &(i->detail), list) {
    list_del(&(p->list));
    kfree(p);
  }
  hash_del(&(i->hlist));
  kfree(i);
}

void refault_record_exit(void)
{
  struct atom *i;
  struct hlist_node *t;
  int bkt;

  unregister_hooks();

  flush_workqueue(system_wq);

  remove_proc_entry(PROC_NAME, NULL);

  mutex_lock(&record_lock);
  hash_for_each_safe(atoms, bkt, t, i, hlist) {
    _destroy_atom(i);
  }
  mutex_unlock(&record_lock);
}

module_init(refault_record_init);
module_exit(refault_record_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("recording refaults about pagecache and swapcache");
