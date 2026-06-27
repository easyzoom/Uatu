// ebpf/watch.bpf.c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// 函数入口时间戳（tid → ns）
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key,   __u32);
    __type(value, __u64);
} entry_times SEC(".maps");

// 事件结构（用户态解析）
struct watch_event {
    __u64 timestamp_ns;
    __u64 duration_ns;
    __u64 args[6];    // RDI RSI RDX RCX R8 R9（System V AMD64 ABI）
    __u64 ret_val;
    __u64 func_addr;  // runtime address of probed function (bpf_get_func_ip)
    __u32 tid;
    __u8  is_exit;    // 0=entry, 1=exit
};

// Ring buffer 输出
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);  // 1MB
} events SEC(".maps");

SEC("uprobe")
int uprobe_entry(struct pt_regs *ctx) {
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    __u64 ts  = bpf_ktime_get_ns();
    bpf_map_update_elem(&entry_times, &tid, &ts, BPF_ANY);

    struct watch_event *ev = bpf_ringbuf_reserve(&events, sizeof(*ev), 0);
    if (!ev) return 0;
    ev->timestamp_ns = ts;
    ev->duration_ns  = 0;
    ev->tid          = tid;
    ev->is_exit      = 0;
    ev->func_addr    = bpf_get_func_ip(ctx);
    ev->args[0] = PT_REGS_PARM1(ctx);
    ev->args[1] = PT_REGS_PARM2(ctx);
    ev->args[2] = PT_REGS_PARM3(ctx);
    ev->args[3] = PT_REGS_PARM4(ctx);
    ev->args[4] = PT_REGS_PARM5(ctx);
    ev->args[5] = PT_REGS_PARM6(ctx);
    ev->ret_val  = 0;
    bpf_ringbuf_submit(ev, 0);
    return 0;
}

SEC("uretprobe")
int uprobe_exit(struct pt_regs *ctx) {
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    __u64 now = bpf_ktime_get_ns();
    __u64 *entry_ts = bpf_map_lookup_elem(&entry_times, &tid);

    struct watch_event *ev = bpf_ringbuf_reserve(&events, sizeof(*ev), 0);
    if (!ev) {
        bpf_map_delete_elem(&entry_times, &tid);  // 修复：清理残留时间戳
        return 0;
    }
    ev->timestamp_ns = now;
    ev->duration_ns  = entry_ts ? (now - *entry_ts) : 0;
    ev->tid          = tid;
    ev->is_exit      = 1;
    ev->func_addr    = bpf_get_func_ip(ctx);
    ev->ret_val      = PT_REGS_RC(ctx);
    __builtin_memset(ev->args, 0, sizeof(ev->args));
    bpf_ringbuf_submit(ev, 0);

    bpf_map_delete_elem(&entry_times, &tid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
