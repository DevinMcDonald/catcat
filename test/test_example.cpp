#include <gtest/gtest.h>

TEST(SmokeTest, TrueIsTrue) { EXPECT_TRUE(true); }

TEST(SmokeTest, BasicArithmetic) { EXPECT_EQ(2 + 2, 4); }

// Sample Change 2
TEST(DemoTest, IntentionallyBreakMe) {
  int kibbles = 100;
  kibbles -= 10; // spend on a tower
  EXPECT_EQ(kibbles, 90);
}
