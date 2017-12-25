#include <uapi/linux/ptrace.h>
#include <linux/mm.h>

struct key_t {
  u32 pid;
  u32 a1;
  u64 p64;
};

BPF_HASH(pfs, struct key_t)
BPF_HASH(pfs_times, struct key_t)

int kprobe__handle_mm_fault(struct pt_regs* ctx,
                        struct vm_area_struct *vma,
                        unsigned long address, unsigned int flags)
{
  struct key_t key = {};
  u64 zero = 0, *val;

  //  if ((flags & VM_FAULT_MAJOR) == VM_FAULT_MAJOR) {
    key.pid = bpf_get_current_pid_tgid() >> 32;
    u64 ts = bpf_ktime_get_ns();
    pfs_times.update(&key, &ts);
    //  }

  return 0;
}
int kretprobe__handle_mm_fault(struct pt_regs* ctx,
                        struct vm_area_struct *vma,
                        unsigned long address, unsigned int flags)
{
  struct key_t key = {};
  u64 zero = 0, *start, *val;
  //  if ((flags & VM_FAULT_MAJOR) == VM_FAULT_MAJOR) {
    key.pid = bpf_get_current_pid_tgid() >> 32;
    val = pfs.lookup_or_init(&key, &zero);

    start = pfs_times.lookup(&key);
    if (start != 0) {
      *val = *val + (bpf_ktime_get_ns() - *start);
      pfs_times.delete(&key);
    }
    //  }

  return 0;
}

TRACEPOINT_PROBE(sched, sched_process_exit) {
  struct key_t key = {};
  key.pid = bpf_get_current_pid_tgid() >> 32;
  pfs.delete(&key);
  return 0;
}

BPF_HASH(sleeps, struct key_t)
BPF_HASH(sleeps_start, struct key_t)
TRACEPOINT_PROBE(raw_syscalls, sys_enter) {
  struct key_t key = {
  p64 : bpf_get_current_pid_tgid(),
  a1 : args->id,
  };
  u64 ts = bpf_ktime_get_ns();
  sleeps_start.update(&key, &ts);

  return 0;
}

TRACEPOINT_PROBE(raw_syscalls, sys_exit) {
  struct key_t key = {
  p64 : bpf_get_current_pid_tgid(),
  a1 : args->id,
  };

  u64 delta = 0, zero = 0;
  u64 *val = sleeps_start.lookup(&key);
  if (val != 0) {
    delta = bpf_ktime_get_ns() - *val;
    sleeps_start.delete(&key);

    key.p64 = 0;
    key.a1 = 0;
    key.pid = bpf_get_current_pid_tgid() >> 32;

    val = sleeps.lookup_or_init(&key, &zero);
    (*val) += delta;
  }
  return 0;
}
