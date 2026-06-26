#include "uatu/engine/attach_engine.h"
#include "uatu/cli/formatter.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// 解析命令行：返回 [cmd, arg1, arg2, ...]
static std::vector<std::string> parse_line(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

// 列出可 attach 的进程
static void list_processes() {
    system("ls /proc | grep -E '^[0-9]+$' | while read pid; do "
           "  exe=$(readlink /proc/$pid/exe 2>/dev/null); "
           "  [ -n \"$exe\" ] && echo \"  pid=$pid  $exe\"; "
           "done | head -20");
}

int main(int argc, char** argv) {
    int pid = -1;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--version" || arg == "-v") {
            std::cout << "uatu " << UATU_VERSION << "\n";
            return 0;
        }
        if (arg == "--pid" && i + 1 < argc)
            pid = std::stoi(argv[++i]);
    }

    if (pid < 0) {
        std::cout << "uatu " << UATU_VERSION << "\n"
                  << "Usage: uatu --pid <PID>\n\n";
        std::cout << "Running processes:\n" << std::flush;
        list_processes();
        return 1;
    }

    std::cout << "uatu " << pid << " attached\n";
    std::cout << "Commands: watch <func>  trace <func>  stack <func>  help  quit\n\n";

    uatu::AttachEngine engine(pid);

    std::string line;
    while (true) {
        std::cout << "uatu> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        auto tokens = parse_line(line);
        if (tokens.empty()) continue;

        const auto& cmd = tokens[0];

        if (cmd == "quit" || cmd == "exit" || cmd == "q") break;

        if (cmd == "help") {
            std::cout << "  watch <func>  [count] [timeout_ms]\n"
                      << "  trace <func>  [count] [timeout_ms]\n"
                      << "  stack <func>  [count] [timeout_ms]\n"
                      << "  quit\n";
            continue;
        }

        if (tokens.size() < 2) {
            std::cout << "Usage: " << cmd << " <function_name>\n";
            continue;
        }

        const auto& func = tokens[1];
        int count      = tokens.size() > 2 ? std::stoi(tokens[2]) : 3;
        int timeout_ms = tokens.size() > 3 ? std::stoi(tokens[3]) : 3000;

        try {
            if (cmd == "watch") {
                auto result = engine.watch_checked(func, count, timeout_ms);
                if (!result) {
                    std::cout << "Error: " << result.error().message << "\n";
                } else {
                    for (auto& ev : *result)
                        std::cout << uatu::cli::format_watch_event(ev) << "\n";
                }
            } else if (cmd == "trace") {
                auto nodes = engine.trace(func, count, timeout_ms);
                for (auto& n : nodes)
                    std::cout << uatu::cli::format_trace_node(n) << "\n";
            } else if (cmd == "stack") {
                auto events = engine.stack(func, count, timeout_ms);
                for (auto& ev : events)
                    std::cout << uatu::cli::format_stack_event(ev) << "\n";
            } else {
                std::cout << "Unknown command: " << cmd << " (type 'help')\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    std::cout << "\nDetached. Bye.\n";
    return 0;
}
