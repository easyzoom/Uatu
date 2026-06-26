#include <gtest/gtest.h>
#include "helpers.h"
#include "uatu/engine/attach_engine.h"

static const std::string FIXTURE =
    std::string(FIXTURE_DIR) + "/target_debug";

TEST(Stack, ReturnsStackEvent) {
    FixtureProcess proc(FIXTURE);
    uatu::AttachEngine engine(proc.pid);
    auto events = engine.stack("fixtures::Calculator::add", 1, 2000);
    // At least one event returned (even if simplified)
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].func_name, "fixtures::Calculator::add");
}
