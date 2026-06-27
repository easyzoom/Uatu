#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace uatu::ebpf {

struct RawEvent {
    uint64_t timestamp_ns;
    uint64_t duration_ns;
    uint64_t args[6];
    uint64_t ret_val;
    uint64_t func_addr;  // runtime address of probed function
    uint32_t tid;
    uint8_t  is_exit;
};

using EventCallback = std::function<void(const RawEvent&)>;

class UprobeLoader {
public:
    // bpf_obj_path: path to watch.bpf.o (compiled eBPF object)
    UprobeLoader(int target_pid, const std::string& bpf_obj_path);
    ~UprobeLoader();

    // Attach uprobe+uretprobe to given virtual address in target process
    void attach(uint64_t func_address);

    // Poll ring buffer for events, call cb for each; blocks up to timeout_ms
    void poll(int timeout_ms, const EventCallback& cb);

    void detach_all();

    // Non-copyable
    UprobeLoader(const UprobeLoader&) = delete;
    UprobeLoader& operator=(const UprobeLoader&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uatu::ebpf
