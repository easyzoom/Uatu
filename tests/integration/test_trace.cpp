#include <gtest/gtest.h>
#include "helpers.h"
#include "uatu/engine/attach_engine.h"

static const std::string FIXTURE =
    std::string(FIXTURE_DIR) + "/target_debug";

TEST(Trace, RootNodeHasDuration) {
    FixtureProcess proc(FIXTURE);
    uatu::AttachEngine engine(proc.pid);
    auto nodes = engine.trace("fixtures::Foo::slow", 1, 4000);
    ASSERT_GE(nodes.size(), 1u);
    EXPECT_EQ(nodes[0].func_name, "fixtures::Foo::slow");
    EXPECT_GT(nodes[0].duration_ns, 0u);
}
