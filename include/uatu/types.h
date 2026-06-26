#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace uatu {

struct FuncInfo {
    std::string name;           // demangled
    std::string mangled_name;
    uint64_t    address{0};     // low_pc (link-time)
    uint64_t    size{0};        // high_pc - low_pc (0 = unknown)
    std::vector<std::string> param_types;
    std::string return_type;
};

struct WatchEvent {
    uint64_t    timestamp_ns{0};
    uint64_t    duration_ns{0};
    std::string func_name;
    std::vector<std::string> params;
    std::string ret_value;
    bool        is_exception{false};
};

struct TraceNode {
    std::string func_name;
    uint64_t    duration_ns{0};
    std::vector<TraceNode> children;
};

struct StackEvent {
    uint64_t    timestamp_ns{0};
    std::string func_name;
    std::vector<std::string> frames;
};

} // namespace uatu
