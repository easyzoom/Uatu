#pragma once
#include "uatu/types.h"
#include <string>

namespace uatu::protocol {

std::string encode(const WatchEvent& ev);
WatchEvent  decode_watch(const std::string& json);

std::string encode_stack(const StackEvent& ev);
StackEvent  decode_stack(const std::string& json);

} // namespace uatu::protocol
