#pragma once
#include "uatu/types.h"
#include <memory>
#include <string>
#include <vector>
#include <tl/expected.hpp>

namespace uatu {

struct EngineError { std::string message; };

// Request early termination of the currently running watch/trace/stack command.
// Safe to call from a SIGINT signal handler.  The flag is cleared automatically
// at the start of the next command so the REPL can continue normally.
void request_stop();

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
