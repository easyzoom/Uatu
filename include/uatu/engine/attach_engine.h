#pragma once
#include "uatu/types.h"
#include <memory>
#include <string>
#include <vector>
#include <tl/expected.hpp>

namespace uatu {

struct EngineError { std::string message; };

class AttachEngine {
public:
    explicit AttachEngine(int pid);
    ~AttachEngine();

    std::vector<WatchEvent> watch(const std::string& func_name,
                                  int max_events, int timeout_ms);

    tl::expected<std::vector<WatchEvent>, EngineError>
    watch_checked(const std::string& func_name, int max_events, int timeout_ms);

    std::vector<TraceNode> trace(const std::string& func_name,
                                 int count, int timeout_ms);

    std::vector<StackEvent> stack(const std::string& func_name,
                                  int count, int timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uatu
