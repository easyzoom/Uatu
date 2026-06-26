#pragma once
#include "uatu/types.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uatu::dwarf {

class SymbolFinder {
public:
    explicit SymbolFinder(const std::string& elf_path);
    ~SymbolFinder();

    std::optional<FuncInfo>    find(const std::string& demangled_name) const;
    std::vector<FuncInfo>      find_regex(const std::string& pattern) const;
    std::optional<std::string> lookup_by_addr(uint64_t addr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uatu::dwarf
