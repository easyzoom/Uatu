#include "uatu/cli/formatter.h"
#include <sstream>
#include <iomanip>

namespace uatu::cli {

std::string format_watch_event(const WatchEvent& ev) {
    std::ostringstream oss;
    double cost_ms = ev.duration_ns / 1e6;
    oss << "ts=" << ev.timestamp_ns
        << "  func=" << ev.func_name
        << "  cost=" << std::fixed << std::setprecision(3) << cost_ms << "ms"
        << "  ret=" << ev.ret_value;
    if (!ev.params.empty()) {
        oss << "\n  params=[";
        for (size_t i = 0; i < ev.params.size(); ++i) {
            if (i) oss << ", ";
            oss << ev.params[i];
        }
        oss << "]";
    }
    return oss.str();
}

std::string format_trace_node(const TraceNode& node, int indent) {
    std::ostringstream oss;
    std::string pad(indent * 2, ' ');
    double ms = node.duration_ns / 1e6;
    oss << pad << "+-" << node.func_name
        << " [" << std::fixed << std::setprecision(3) << ms << "ms]\n";
    for (auto& child : node.children)
        oss << format_trace_node(child, indent + 1);
    return oss.str();
}

std::string format_stack_event(const StackEvent& ev) {
    std::ostringstream oss;
    oss << "func=" << ev.func_name << "\n";
    for (size_t i = 0; i < ev.frames.size(); ++i)
        oss << "  [" << i << "] " << ev.frames[i] << "\n";
    return oss.str();
}

} // namespace uatu::cli
