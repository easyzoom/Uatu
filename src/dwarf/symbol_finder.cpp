#include "uatu/dwarf/symbol_finder.h"
#include <elfutils/libdw.h>
#include <dwarf.h>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <vector>
#include <string>

namespace uatu::dwarf {

struct SymbolFinder::Impl {
    int    fd{-1};
    Dwarf* dw{nullptr};
    mutable std::vector<FuncInfo> cache_;
    mutable std::once_flag         cache_flag_;

    // Demangle a mangled C++ name; return as-is if demangling fails.
    std::string demangle(const char* mangled) const {
        if (!mangled || mangled[0] == '\0') return "";
        int status = 0;
        char* d = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
        if (status == 0 && d) {
            std::string r(d);
            free(d);
            return r;
        }
        return mangled;
    }

    // Strip parameter list from demangled name so we can match by base
    // "fixtures::Calculator::add(int, int)" -> "fixtures::Calculator::add"
    static std::string strip_params(const std::string& demangled) {
        auto pos = demangled.find('(');
        if (pos == std::string::npos) return demangled;
        return demangled.substr(0, pos);
    }

    // Resolve the type name for a formal parameter type DIE.
    std::string resolve_type_name(Dwarf_Die* type_die) const {
        if (!type_die) return "?";
        int tag = dwarf_tag(type_die);

        // For pointer/reference/const/volatile qualifiers, recurse into the
        // referenced type.
        if (tag == DW_TAG_pointer_type || tag == DW_TAG_reference_type ||
            tag == DW_TAG_rvalue_reference_type ||
            tag == DW_TAG_const_type    || tag == DW_TAG_volatile_type ||
            tag == DW_TAG_restrict_type || tag == DW_TAG_typedef) {
            Dwarf_Attribute attr;
            if (dwarf_attr(type_die, DW_AT_type, &attr)) {
                Dwarf_Die inner;
                if (dwarf_formref_die(&attr, &inner)) {
                    std::string inner_name = resolve_type_name(&inner);
                    if (tag == DW_TAG_pointer_type)   return inner_name + "*";
                    if (tag == DW_TAG_reference_type) return inner_name + "&";
                    if (tag == DW_TAG_rvalue_reference_type) return inner_name + "&&";
                    if (tag == DW_TAG_const_type)     return "const " + inner_name;
                    if (tag == DW_TAG_volatile_type)  return "volatile " + inner_name;
                    return inner_name;
                }
            }
            return "?";
        }

        const char* n = dwarf_diename(type_die);
        return n ? n : "?";
    }

    // Collect all concrete (non-declaration) subprograms from all CUs.
    const std::vector<FuncInfo>& all() const {
        std::call_once(cache_flag_, [this] {
            Dwarf_Off off = 0, next_off;
            size_t hdr_size;
            while (dwarf_nextcu(dw, off, &next_off, &hdr_size,
                                nullptr, nullptr, nullptr) == 0) {
                Dwarf_Die cudie;
                if (dwarf_offdie(dw, off + hdr_size, &cudie)) {
                    collect_subprograms(&cudie);
                }
                off = next_off;
            }
        });
        return cache_;
    }

    // Recursively traverse DIEs looking for DW_TAG_subprogram with low_pc.
    void collect_subprograms(Dwarf_Die* parent) const {
        Dwarf_Die child;
        if (dwarf_child(parent, &child) != 0) return;

        do {
            int tag = dwarf_tag(&child);

            if (tag == DW_TAG_subprogram) {
                process_subprogram(&child);
                // Also recurse for nested functions (rare but valid).
                collect_subprograms(&child);
                continue;
            }

            // Recurse into namespace/class/struct: GCC/Clang sometimes places
            // concrete member function definitions (with DW_AT_low_pc) inside
            // these containers rather than at the CU top level.
            if (tag == DW_TAG_namespace   ||
                tag == DW_TAG_class_type  ||
                tag == DW_TAG_structure_type) {
                collect_subprograms(&child);
                continue;
            }

            // Recurse into lexical blocks and inlined instances.
            if (tag == DW_TAG_lexical_block ||
                tag == DW_TAG_inlined_subroutine) {
                collect_subprograms(&child);
            }
        } while (dwarf_siblingof(&child, &child) == 0);
    }

    void process_subprogram(Dwarf_Die* die) const {
        // Skip declarations (no address).
        Dwarf_Addr lo;
        if (dwarf_lowpc(die, &lo) != 0) return;
        if (lo == 0) return;  // skip 0-address entries

        // Try to get the linkage (mangled) name.  dwarf_attr_integrate
        // automatically follows DW_AT_specification/DW_AT_abstract_origin.
        Dwarf_Attribute attr;
        const char* mangled = nullptr;
        if (dwarf_attr_integrate(die, DW_AT_linkage_name, &attr) ||
            dwarf_attr_integrate(die, DW_AT_MIPS_linkage_name, &attr)) {
            mangled = dwarf_formstring(&attr);
        }

        std::string demangled;
        std::string mangled_str;
        if (mangled && mangled[0] != '\0') {
            mangled_str = mangled;
            demangled = strip_params(demangle(mangled));
        } else {
            // Fallback: use DW_AT_name (may be unqualified).
            const char* n = dwarf_diename(die);
            if (!n) return;
            mangled_str = n;
            demangled = n;
        }

        if (demangled.empty()) return;

        // Get function size from DW_AT_high_pc
        Dwarf_Addr hi = 0;
        int hi_form = dwarf_highpc(die, &hi);
        // hi_form==0: hi is absolute address; hi_form==1: hi is size (offset)
        uint64_t func_size = 0;
        if (hi_form == 0 && hi > lo)       func_size = hi - lo;
        else if (hi_form == 1)             func_size = hi;

        FuncInfo info;
        info.mangled_name = mangled_str;
        info.name         = demangled;
        info.address      = lo;
        info.size         = func_size;

        // Collect parameter types from formal_parameter children.
        // dwarf_attr_integrate on the *concrete* definition's params may
        // follow abstract_origin/specification.  We iterate children of
        // the concrete DIE; then for each formal_parameter we resolve type.
        Dwarf_Die param;
        if (dwarf_child(die, &param) == 0) {
            do {
                if (dwarf_tag(&param) != DW_TAG_formal_parameter) continue;

                // Skip the implicit 'this' parameter.
                Dwarf_Attribute art_attr;
                if (dwarf_attr(&param, DW_AT_artificial, &art_attr)) {
                    bool artificial = false;
                    dwarf_formflag(&art_attr, &artificial);
                    if (artificial) continue;
                }

                // Resolve type reference.
                Dwarf_Attribute type_attr;
                if (dwarf_attr_integrate(&param, DW_AT_type, &type_attr)) {
                    Dwarf_Die type_die;
                    if (dwarf_formref_die(&type_attr, &type_die)) {
                        info.param_types.push_back(resolve_type_name(&type_die));
                    } else {
                        info.param_types.push_back("?");
                    }
                }
            } while (dwarf_siblingof(&param, &param) == 0);
        }

        cache_.push_back(std::move(info));
    }
};

SymbolFinder::SymbolFinder(const std::string& elf_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->fd = open(elf_path.c_str(), O_RDONLY);
    if (impl_->fd < 0)
        throw std::runtime_error("cannot open: " + elf_path);

    impl_->dw = dwarf_begin(impl_->fd, DWARF_C_READ);
    if (!impl_->dw) {
        close(impl_->fd);
        impl_->fd = -1;
        throw std::runtime_error("no DWARF debug info in: " + elf_path);
    }
}

SymbolFinder::~SymbolFinder() {
    if (impl_->dw)      dwarf_end(impl_->dw);
    if (impl_->fd >= 0) close(impl_->fd);
}

std::optional<FuncInfo> SymbolFinder::find(const std::string& name) const {
    for (auto& info : impl_->all())
        if (info.name == name) return info;
    return std::nullopt;
}

std::vector<FuncInfo> SymbolFinder::find_regex(const std::string& pattern) const {
    std::regex re(pattern);
    std::vector<FuncInfo> out;
    for (auto& info : impl_->all())
        if (std::regex_search(info.name, re)) out.push_back(info);
    return out;
}

std::optional<std::string> SymbolFinder::lookup_by_addr(uint64_t addr) const {
    // Range-based lookup: addr may be anywhere inside the function body
    // (return addresses point after the call instruction, not at entry point).
    for (auto& info : impl_->all()) {
        if (info.address == 0) continue;
        if (addr == info.address) return info.name;
        if (info.size > 0 && addr >= info.address && addr < info.address + info.size)
            return info.name;
    }
    return std::nullopt;
}

} // namespace uatu::dwarf
