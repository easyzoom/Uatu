#pragma once
#include "uatu/types.h"
#include <string>

namespace uatu::cli {

std::string format_watch_event(const WatchEvent& ev);
std::string format_trace_node(const TraceNode& node, int indent = 0);
std::string format_stack_event(const StackEvent& ev);

} // namespace uatu::cli
