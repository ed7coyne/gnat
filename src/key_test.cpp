
#include "key.h"

#include <gtest/gtest.h>

TEST(KeyTest, EncodeFullLength) {
    constexpr auto encoded = gnat::key::Encode("TESTTEST");
    ASSERT_EQ(6076276550747243860ul, encoded);
}

TEST(KeyTest, EncodeShort) {
    constexpr auto encoded = gnat::key::Encode("TEST");
    ASSERT_EQ(1414743380ul, encoded);
}

TEST(KeyTest, DecodeFullLength) {
    ASSERT_EQ("TESTTEST", gnat::key::Decode(6076276550747243860ul));
}

TEST(KeyTest, DecodeShortLength) {
    ASSERT_EQ("TEST", gnat::key::Decode(1414743380ul));
}

TEST(KeyTest, EncodeDecodeSpecial) {
    ASSERT_EQ("T", gnat::key::Decode(gnat::key::Encode("T")));
    ASSERT_EQ("T T", gnat::key::Decode(gnat::key::Encode("T T")));
    ASSERT_EQ("0", gnat::key::Decode(gnat::key::Encode("0")));
}

TEST(KeyTest, EncodeDecodeString) {
    ASSERT_EQ("T", gnat::key::Decode(gnat::key::EncodeString("T", 1)));
    ASSERT_EQ("T T", gnat::key::Decode(gnat::key::EncodeString("T T", 3)));
    ASSERT_EQ("0", gnat::key::Decode(gnat::key::EncodeString("0", 1)));
    ASSERT_EQ("t/test", gnat::key::Decode(gnat::key::EncodeString("t/test", 6)));
}
