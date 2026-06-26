#include "uatu/protocol/json_codec.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace uatu::protocol {

std::string encode(const WatchEvent& ev) {
    nlohmann::json j;
    j["type"]         = "watch";
    j["ts"]           = ev.timestamp_ns;
    j["duration_ns"]  = ev.duration_ns;
    j["func"]         = ev.func_name;
    j["params"]       = ev.params;
    j["ret"]          = ev.ret_value;
    j["is_exception"] = ev.is_exception;
    return j.dump();
}

WatchEvent decode_watch(const std::string& s) {
    try {
        auto j = nlohmann::json::parse(s);
        WatchEvent ev;
        ev.timestamp_ns = j.value("ts",           uint64_t{0});
        ev.duration_ns  = j.value("duration_ns",  uint64_t{0});
        ev.func_name    = j.value("func",          std::string{});
        ev.params       = j.value("params",        std::vector<std::string>{});
        ev.ret_value    = j.value("ret",           std::string{});
        ev.is_exception = j.value("is_exception",  false);
        return ev;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("JSON decode error: ") + e.what());
    }
}

std::string encode_stack(const StackEvent& ev) {
    nlohmann::json j;
    j["type"]   = "stack";
    j["ts"]     = ev.timestamp_ns;
    j["func"]   = ev.func_name;
    j["frames"] = ev.frames;
    return j.dump();
}

StackEvent decode_stack(const std::string& s) {
    try {
        auto j = nlohmann::json::parse(s);
        StackEvent ev;
        ev.timestamp_ns = j.value("ts",     uint64_t{0});
        ev.func_name    = j.value("func",   std::string{});
        ev.frames       = j.value("frames", std::vector<std::string>{});
        return ev;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("JSON decode error: ") + e.what());
    }
}

} // namespace uatu::protocol
