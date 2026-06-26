#include <gtest/gtest.h>
#include "uatu/protocol/json_codec.h"

TEST(JsonCodec, RoundtripWatchEvent) {
    uatu::WatchEvent ev;
    ev.timestamp_ns = 1234567890ULL;
    ev.duration_ns  = 230000ULL;
    ev.func_name    = "fixtures::Foo::bar";
    ev.params       = {"5", "\"hello\""};
    ev.ret_value    = "\"hello5\"";
    ev.is_exception = false;

    auto json_str = uatu::protocol::encode(ev);
    auto decoded  = uatu::protocol::decode_watch(json_str);

    EXPECT_EQ(decoded.func_name,   ev.func_name);
    EXPECT_EQ(decoded.duration_ns, ev.duration_ns);
    EXPECT_EQ(decoded.params.size(), 2u);
    EXPECT_EQ(decoded.ret_value,   ev.ret_value);
    EXPECT_FALSE(decoded.is_exception);
}

TEST(JsonCodec, RoundtripStackEvent) {
    uatu::StackEvent ev;
    ev.timestamp_ns = 9999ULL;
    ev.func_name    = "fixtures::Calculator::add";
    ev.frames       = {"main()", "fixtures::Calculator::add(int, int)"};

    auto json_str = uatu::protocol::encode_stack(ev);
    auto decoded  = uatu::protocol::decode_stack(json_str);

    EXPECT_EQ(decoded.func_name, ev.func_name);
    EXPECT_EQ(decoded.frames.size(), 2u);
    EXPECT_EQ(decoded.frames[1], ev.frames[1]);
}

TEST(JsonCodec, WatchEventTypeField) {
    uatu::WatchEvent ev;
    ev.func_name = "test::func";
    auto json_str = uatu::protocol::encode(ev);
    EXPECT_NE(json_str.find("\"watch\""), std::string::npos);
}

TEST(JsonCodec, ExceptionFlagRoundtrip) {
    uatu::WatchEvent ev;
    ev.is_exception = true;
    ev.func_name    = "test::func";
    auto json_str = uatu::protocol::encode(ev);
    auto decoded  = uatu::protocol::decode_watch(json_str);
    EXPECT_TRUE(decoded.is_exception);
}

TEST(JsonCodec, DecodeWatchThrowsOnInvalidJson) {
    EXPECT_THROW(
        uatu::protocol::decode_watch("not-valid-json{{{"),
        std::runtime_error
    );
}

TEST(JsonCodec, DecodeStackThrowsOnInvalidJson) {
    EXPECT_THROW(
        uatu::protocol::decode_stack(""),
        std::runtime_error
    );
}
