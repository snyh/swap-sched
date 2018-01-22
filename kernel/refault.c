#include <linux/stringhash.h>
#include <linux/hashtable.h>
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

struct record_task {
  pid_t pid;
  char name[NAME_SIZE];
  unsigned long addr;
  struct file *file;
  bool is_release;
  struct work_struct w;
};

struct kv {
  struct list_head list;
  unsigned long addr;
  char vm_file_name[NAME_SIZE];
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

static void do_record_refault(struct record_task *t);
static void do_release(pid_t pid);
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

static void work_func(struct work_struct *work)
{
  struct record_task *t = container_of(work, struct record_task, w);
  if (t->is_release) {
    do_release(t->pid);
  } else {
    do_record_refault(t);
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
  i->file = 0;
  INIT_WORK(&(i->w), work_func);

  schedule_work(&(i->w));
}

void prepare_record_refault(unsigned long addr, struct file* file)
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
  if (file) {
    i->file = get_file(file);
  }

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

void _record_detail(struct atom* i, struct record_task *t)
{
  struct kv* p;
  struct list_head *head;
  unsigned long addr = t->addr;

  if (t->file) {
    head = &(i->file_detail);
  } else {
    head = &(i->anon_detail);
  }

  list_for_each_entry(p, head, list) {
    if (p->addr == addr) {
      p->count++;
      _atom_count_sum(i, p->count);
      if (t->file)
        put_filp(t->file);
      return;
    }
  }
  if (t->file)
    put_filp(t->file);

  p = kzalloc(sizeof(*p), GFP_ATOMIC);
  if (unlikely(!p))
    return;
  if (t->file) {
    const char* tmp = file_path(t->file, t->name, NAME_SIZE);
    if (IS_ERR(tmp)) {
      memcpy(p->vm_file_name, "tooloong", 9);
    } else  {
      memcpy(p->vm_file_name, tmp, min(strlen(tmp), NAME_SIZE));
    }
  }
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
static void do_record_refault(struct record_task *t)
{
  struct atom *i = NULL;

  mutex_lock(&record_lock);
  hash_for_each_possible(atoms, i, hlist, t->pid) {
    if (i->pid == t->pid) {
      _record_detail(i, t);
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
  _record_detail(i, t);
  hash_add(atoms, &(i->hlist), t->pid);
  mutex_unlock(&record_lock);
}

void ggoo(struct seq_file* file, const char* t, struct list_head* head, int min)
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
  seq_printf(file, "\tUse %ldKB reduce %dMB IO(%ld%%) for %s",
             good_num * 4 , good_sum * 4 / 1024, good_sum*100 / all,
             t);
  list_for_each_entry(p, head, list) {
    all += p->count;
    if (p->count > min) {
      seq_printf(file, " %s %llx %d\n", p->vm_file_name, p->addr, p->count);
    }
  }
}


void _record_dump_detail(struct seq_file* file, struct atom *i)
{
  struct kv *p;
  int greater_than_one = 0, pos = 2;
  unsigned long file_all = 0, anon_all = 0;
  unsigned short memcg_id = task_memcg_id(i->pid);
  if (memcg_id == 0) {
    _destroy_atom(i);
    return;
  }

  for (; pos < MAX_COUNT_SIZE; pos++) {
    greater_than_one += i->count_sum[pos];
  }
  if (greater_than_one == 0) {
    return;
  }

  list_for_each_entry(p, &(i->anon_detail), list) {
    anon_all += p->count;
  }

  list_for_each_entry(p, &(i->file_detail), list) {
    file_all += p->count;
  }

  seq_printf(file, "%d\t%ld\t%ld%%\t%d\t%s", memcg_id, anon_all+file_all, anon_all,
             i->pid, i->name);

  ggoo(file, "file", &(i->file_detail), 100);
  ggoo(file, "anonymous", &(i->anon_detail), 100);

  /* seq_putc(file, '\t'); */
  /* for (pos = 0; pos < MAX_COUNT_SIZE; pos++) { */
  /*   if (i->count_sum[pos] > 0) */
  /*     seq_printf(file, " [%d:%d]", pos+1, i->count_sum[pos]); */
  /* } */
  seq_putc(file, '\n');
}

void record_dump(struct seq_file* file)
{
  struct atom *i;
  struct hlist_node *tmp;
  int bkt;

  seq_printf(file, "MEMID\tALL\tAnon\tPID\tCOMM\n");

  mutex_lock(&record_lock);
  hash_for_each_safe(atoms, bkt, tmp, i, hlist) {
    if (current->pid == i->pid) {
      continue;
    }
    _record_dump_detail(file, i);
  }
  mutex_unlock(&record_lock);
}


int hook_entry_swapin_refault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  struct readswapin_args *d;
  struct vm_area_struct *vma;
  if (!current->mm)
    return 1;

  vma = (struct vm_area_struct*)(void*)(unsigned long)(regs->dx);
  if (!vma || (vma_is_anonymous(vma) && vma->vm_pgoff > 0)) {
    // if the vm_area_struct is a shmmem pseudo vma
    return 1;
  }

  d = (struct readswapin_args*)ri->data;
  d->address = (unsigned long)regs->cx;
  BUG_ON(!d->address);
  d->miss_from_swapcache = (void*)(regs->r8);
  return 1;
}

int hook_ret_swapin_refault(struct kretprobe_instance *ri, struct pt_regs *regs)
{
  int __maybe_unused retval = regs_return_value(regs);
  struct readswapin_args *d = (struct readswapin_args*)ri->data;
  bool *miss = d->miss_from_swapcache;
  if (miss && *miss) {
    prepare_record_refault(d->address, 0);
  }
  return 0;
}

int hook_filemap_refault(struct kprobe *p, struct pt_regs *regs)
{

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0))
  struct vm_fault *vmf = (void*)(regs->di);
  struct vm_area_struct* vma = vmf->vma;
  unsigned long addr = (unsigned long)(vmf->address);
#else
  struct vm_area_struct* vma = (void*)(regs->di);
  struct vm_fault *vmf = (void*)(regs->si);
  unsigned long addr = (unsigned long)(vmf->virtual_address);
#endif
  if (!current->mm) {
    return 1;
  }
  prepare_record_refault(addr, vma->vm_file);
  return 0;
}

int hook_do_exit(struct kprobe *p, struct pt_regs *regs)
{
  if (!current || !current->mm) {
    return 1;
  }
  prepare_release(current->pid);
  return 0;
}

#define NUM_PROBE 2
static struct kprobe pp[NUM_PROBE] = {
  {
    .symbol_name = "shmem_fault",
    .pre_handler = hook_filemap_refault,
  },
  {
    .symbol_name = "filemap_fault",
    .pre_handler = hook_filemap_refault,
  },
};

#define NUM_RETPROBE 1
static struct kretprobe retpp[NUM_RETPROBE] = {
  {
    .handler = hook_ret_swapin_refault,
    .entry_handler = hook_entry_swapin_refault,
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

void refault_record_exit(void)
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

module_init(refault_record_init);
module_exit(refault_record_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("recording refaults about pagecache and swapcache");
