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

DEFINE_HASHTABLE(atoms, 8);

#define NAME_SIZE 128

struct record_task {
  struct file* file;
  pgoff_t offset;;
  struct work_struct w;
};

DEFINE_MUTEX(record_lock);

static void _do_record_fault(const char* name, pgoff_t offset);

static void work_func(struct work_struct *work)
{
  static char buf[NAME_SIZE];
  char* name = NULL;
  struct record_task *t = container_of(work, struct record_task, w);

  mutex_lock(&record_lock);

  name = file_path(t->file, buf, sizeof(buf));
  if (!IS_ERR(name)) {
    _do_record_fault(name, t->offset);
  }
  fput(t->file);
  kfree(t);

  mutex_unlock(&record_lock);
}

void prepare_record_fault(struct file  *f, pgoff_t offset)
{
  struct record_task *i = 0;
  i = kmalloc(sizeof(*i), GFP_ATOMIC);
  i->file = f;
  i->offset = offset;
  INIT_WORK(&(i->w), work_func);
  queue_work(system_freezable_wq, &(i->w));
}

struct kv {
  struct list_head list;
  pgoff_t offset;
  u32 count;
};

struct atom {
  struct hlist_node hlist;
  char name[NAME_SIZE];
  u32 sum;
  struct list_head detail;
};

void _record_detail(struct atom* i, pgoff_t offset)
{
  struct kv* p;
  i->sum++;
  list_for_each_entry(p, &(i->detail), list) {
    if (p->offset == offset) {
      p->count++;
      return;
    }
  }
  p = kmalloc(sizeof(*p), GFP_ATOMIC);
  p->offset = offset;
  p->count = 1;
  list_add(&(p->list), &(i->detail));
}


static void _do_record_fault(const char* name, pgoff_t offset)
{
  struct atom *i = NULL;
  u64 key = hashlen_string(&atoms, name);

  BUG_ON(!name);

  hash_for_each_possible(atoms, i, hlist, key) {
     _record_detail(i, offset);
    return;
  }

  i = kzalloc(sizeof(*i), GFP_ATOMIC);
  INIT_LIST_HEAD(&(i->detail));
  memcpy(i->name, name, NAME_SIZE);
  _record_detail(i, offset);
  hash_add(atoms, &(i->hlist), key);
}

void _record_dump_detail(struct seq_file* file, struct atom *i)
{
  struct kv *p;
  seq_printf(file, "%s\t%d", i->name, i->sum);

  list_for_each_entry(p, &(i->detail), list) {
    seq_printf(file, "\t%ld:%d", p->offset, p->count);
  }
  seq_putc(file, '\n');
}

void record_dump(struct seq_file* file)
{
  struct atom *i;
  int bkt;
  mutex_lock(&record_lock);
  hash_for_each(atoms, bkt, i, hlist) {
    _record_dump_detail(file, i);
  }
  mutex_unlock(&record_lock);
}

int hook_filemap_fault(struct kprobe *p, struct pt_regs *regs)
{
  struct vm_area_struct *vma = (void*)(regs->di);
  struct vm_fault *vmf = (void*)(regs->si);

  struct file *file;
  struct address_space *mapping;
  pgoff_t offset;
  struct page *page;
  void **pagep;

  BUG_ON(!vma);
  file = vma->vm_file;
  BUG_ON(!file);
  mapping = file->f_mapping;
  BUG_ON(!mapping);
  BUG_ON(!vmf);
  offset = vmf->pgoff;

  spin_lock(&mapping->tree_lock);
  pagep = radix_tree_lookup_slot(&mapping->page_tree, offset);
  if (pagep) {
    page = radix_tree_deref_slot(pagep);
    if (page)   {
      spin_unlock(&mapping->tree_lock);
      return 0;
    }
  }
  spin_unlock(&mapping->tree_lock);

  prepare_record_fault(get_file(file), offset);
  return 0;
}

static struct kprobe pp = {
  .symbol_name = "filemap_fault",
  .pre_handler = hook_filemap_fault,
};


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


  ret = register_kprobe(&pp);
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
  hash_del(&(i->hlist));
  list_for_each_entry_safe(p, t, &(i->detail), list) {
    kfree(p);
  }
  kfree(i);
}

void fault_record_exit(void)
{
  struct atom *i;
  struct hlist_node *t;
  int bkt;

  unregister_kprobe(&pp);

  flush_workqueue(system_freezable_wq);

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
