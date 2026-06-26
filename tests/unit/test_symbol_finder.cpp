#include <gtest/gtest.h>
#include "uatu/dwarf/symbol_finder.h"

static const std::string FIXTURE_PATH =
    std::string(FIXTURE_DIR) + "/target_debug";

TEST(SymbolFinder, FindByDemangledName) {
    uatu::dwarf::SymbolFinder finder(FIXTURE_PATH);
    auto info = finder.find("fixtures::Calculator::add");
    ASSERT_TRUE(info.has_value());
    EXPECT_NE(info->address, 0u);
    EXPECT_GE(info->param_types.size(), 2u);
}

TEST(SymbolFinder, FindByRegex) {
    uatu::dwarf::SymbolFinder finder(FIXTURE_PATH);
    auto results = finder.find_regex("fixtures::Foo::.*");
    EXPECT_GE(results.size(), 2u);
}

TEST(SymbolFinder, ReturnEmptyForUnknown) {
    uatu::dwarf::SymbolFinder finder(FIXTURE_PATH);
    auto info = finder.find("nonexistent::func");
    EXPECT_FALSE(info.has_value());
}

TEST(SymbolFinder, LookupByAddr) {
    uatu::dwarf::SymbolFinder finder(FIXTURE_PATH);
    auto info = finder.find("fixtures::Calculator::add");
    ASSERT_TRUE(info.has_value());
    auto name = finder.lookup_by_addr(info->address);
    ASSERT_TRUE(name.has_value());
    EXPECT_NE(name->find("Calculator"), std::string::npos);
}

TEST(SymbolFinder, ThrowsOnNoDwarf) {
    EXPECT_THROW(
        uatu::dwarf::SymbolFinder("/bin/ls"),
        std::runtime_error
    );
}
