#include "uatu/ebpf/uprobe_loader.h"
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstring>

namespace uatu::ebpf {

struct UprobeLoader::Impl {
    int               pid{-1};
    bpf_object*       obj{nullptr};
    ring_buffer*      rb{nullptr};
    std::vector<bpf_link*> links;
    EventCallback     pending_cb;

    static int on_event(void* ctx, void* data, size_t sz) {
        if (sz < sizeof(RawEvent)) return 0;
        auto* self = static_cast<Impl*>(ctx);
        if (self->pending_cb)
            self->pending_cb(*static_cast<const RawEvent*>(data));
        return 0;
    }
};

UprobeLoader::UprobeLoader(int pid, const std::string& bpf_obj_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->pid = pid;

    impl_->obj = bpf_object__open(bpf_obj_path.c_str());
    if (!impl_->obj)
        throw std::runtime_error("bpf_object__open failed: " + bpf_obj_path);

    if (bpf_object__load(impl_->obj) != 0) {
        bpf_object__close(impl_->obj);
        impl_->obj = nullptr;
        throw std::runtime_error("bpf_object__load failed — need CAP_BPF or root");
    }
}

UprobeLoader::~UprobeLoader() {
    detach_all();
    if (impl_->rb)  ring_buffer__free(impl_->rb);
    if (impl_->obj) bpf_object__close(impl_->obj);
}

void UprobeLoader::attach(uint64_t func_address) {
    // Resolve target exe path
    char link_path[64];
    char exe_path[512];
    snprintf(link_path, sizeof(link_path), "/proc/%d/exe", impl_->pid);
    ssize_t n = readlink(link_path, exe_path, sizeof(exe_path) - 1);
    if (n <= 0)
        throw std::runtime_error("readlink /proc/pid/exe failed");
    exe_path[n] = '\0';

    bpf_program* entry_prog =
        bpf_object__find_program_by_name(impl_->obj, "uprobe_entry");
    bpf_program* exit_prog =
        bpf_object__find_program_by_name(impl_->obj, "uprobe_exit");
    if (!entry_prog || !exit_prog)
        throw std::runtime_error("uprobe programs not found in BPF object");

    bpf_link* entry_link =
        bpf_program__attach_uprobe(entry_prog, false, impl_->pid,
                                   exe_path, func_address);
    if (!entry_link)
        throw std::runtime_error("attach entry uprobe failed");
    impl_->links.push_back(entry_link);  // 立即入队，异常时析构器会清理

    bpf_link* exit_link =
        bpf_program__attach_uprobe(exit_prog, true, impl_->pid,
                                   exe_path, func_address);
    if (!exit_link)
        throw std::runtime_error("attach exit uprobe failed");  // entry_link 已在 links 中，会被 detach_all 清理
    impl_->links.push_back(exit_link);
}

void UprobeLoader::poll(int timeout_ms, const EventCallback& cb) {
    impl_->pending_cb = cb;

    if (!impl_->rb) {
        bpf_map* rb_map =
            bpf_object__find_map_by_name(impl_->obj, "events");
        if (!rb_map)
            throw std::runtime_error("ring buffer map 'events' not found");

        impl_->rb = ring_buffer__new(
            bpf_map__fd(rb_map),
            Impl::on_event, impl_.get(), nullptr);
        if (!impl_->rb)
            throw std::runtime_error("ring_buffer__new failed");
    }

    ring_buffer__poll(impl_->rb, timeout_ms);
}

void UprobeLoader::detach_all() {
    for (auto* link : impl_->links)
        bpf_link__destroy(link);
    impl_->links.clear();
}

} // namespace uatu::ebpf
