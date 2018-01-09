#include <linux/kprobes.h>
#include <linux/ptrace.h>

static void _hook_for_recording_pagefault(struct page *page,
                        struct mem_cgroup *memcg,
                        bool lrucare, bool compound)
{
  struct page_group *pg;

  if (!memcg || compound || (current->mm == 0)) {
    goto end;
  }

  pg = find_pg(memcg);
  if (!pg || pg_full(pg)) {
    goto end;
  }

  pg_inc(pg, page_to_pfn(page));

 end:
  jprobe_return();
}

static struct jprobe pp = {
  .kp = {
    .symbol_name = "mem_cgroup_commit_charge",
  },
  .entry = (kprobe_opcode_t *) _hook_for_recording_pagefault,
};

static int init_hook(void)
{
  int ret = -1;
  ret = register_jprobe(&pp);
  if (ret < 0) {
        printk(KERN_INFO "register_jprobe failed, returned %d\n",
                ret);
        return -1;
  }
  disable_kprobe(&pp.kp);
  enable_kprobe(&pp.kp);
  return 0;
}

static void exit_hook(void)
{
  unregister_jprobe(&pp);
}
