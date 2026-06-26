// ebpf/trace.bpf.c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// per-thread call depth
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key,   __u32);
    __type(value, __u32);
} call_depth SEC(".maps");

// per-thread entry timestamp
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key,   __u32);
    __type(value, __u64);
} entry_times SEC(".maps");

struct trace_event {
    __u64 timestamp_ns;
    __u64 duration_ns;
    __u32 tid;
    __u32 depth;
    __u8  is_exit;
    __u8  pad[3];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} events SEC(".maps");

SEC("uprobe")
int trace_entry(struct pt_regs *ctx) {
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    __u64 ts  = bpf_ktime_get_ns();

    __u32 *d = bpf_map_lookup_elem(&call_depth, &tid);
    __u32  depth = d ? (*d + 1) : 0;
    bpf_map_update_elem(&call_depth, &tid, &depth, BPF_ANY);
    bpf_map_update_elem(&entry_times, &tid, &ts, BPF_ANY);

    struct trace_event *ev = bpf_ringbuf_reserve(&events, sizeof(*ev), 0);
    if (!ev) return 0;
    ev->timestamp_ns = ts;
    ev->duration_ns  = 0;
    ev->tid          = tid;
    ev->depth        = depth;
    ev->is_exit      = 0;
    bpf_ringbuf_submit(ev, 0);
    return 0;
}

SEC("uretprobe")
int trace_exit(struct pt_regs *ctx) {
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    __u64 now = bpf_ktime_get_ns();

    __u64 *entry_ts = bpf_map_lookup_elem(&entry_times, &tid);
    __u32 *d = bpf_map_lookup_elem(&call_depth, &tid);
    __u32 depth = d ? *d : 0;
    if (d && *d > 0) { __u32 nd = *d - 1; bpf_map_update_elem(&call_depth, &tid, &nd, BPF_ANY); }
    else bpf_map_delete_elem(&call_depth, &tid);

    struct trace_event *ev = bpf_ringbuf_reserve(&events, sizeof(*ev), 0);
    if (!ev) {
        bpf_map_delete_elem(&entry_times, &tid);
        return 0;
    }
    ev->timestamp_ns = now;
    ev->duration_ns  = entry_ts ? (now - *entry_ts) : 0;
    ev->tid          = tid;
    ev->depth        = depth;
    ev->is_exit      = 1;
    bpf_ringbuf_submit(ev, 0);
    bpf_map_delete_elem(&entry_times, &tid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
