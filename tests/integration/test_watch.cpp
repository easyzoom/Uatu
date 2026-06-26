#include <gtest/gtest.h>
#include "helpers.h"
#include "uatu/engine/attach_engine.h"

static const std::string FIXTURE_DEBUG =
    std::string(FIXTURE_DIR) + "/target_debug";
static const std::string FIXTURE_STRIP =
    std::string(FIXTURE_DIR) + "/target_strip";

TEST(WatchDebug, CapturesReturnValue) {
    FixtureProcess proc(FIXTURE_DEBUG);
    ASSERT_GT(proc.pid, 0);

    uatu::AttachEngine engine(proc.pid);
    auto events = engine.watch("fixtures::Calculator::add",
                               /*max_events=*/2, /*timeout_ms=*/3000);
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].func_name, "fixtures::Calculator::add");
    EXPECT_EQ(events[0].ret_value, "3");
    EXPECT_GT(events[0].duration_ns, 0u);
}

TEST(WatchStrip, ReturnsNoDwarfError) {
    FixtureProcess proc(FIXTURE_STRIP);
    ASSERT_GT(proc.pid, 0);

    uatu::AttachEngine engine(proc.pid);
    auto result = engine.watch_checked("fixtures::Calculator::add", 1, 500);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("DWARF"), std::string::npos);
}
