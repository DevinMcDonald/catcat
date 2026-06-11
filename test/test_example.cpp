#include <gtest/gtest.h>

// Include whatever headers expose testable logic, for example:
// #include "game/game.h"

// ── Placeholder tests (replace with real game logic) ─────────────────────────

// Example: if your Game class has a starting lives count
// TEST(GameTest, StartsWithNineLives) {
//     Game g;
//     EXPECT_EQ(g.GetLives(), 9);
// }

// Example: enemy health scaling
// TEST(EnemyTest, HealthScalesWithWave) {
//     Enemy e(EnemyType::Rat, /*wave=*/10);
//     EXPECT_GT(e.GetHealth(), Enemy(EnemyType::Rat, /*wave=*/1).GetHealth());
// }

// Smoke test so the pipeline has something real to run right now
TEST(SmokeTest, TrueIsTrue) { EXPECT_TRUE(true); }

TEST(SmokeTest, BasicArithmetic) { EXPECT_EQ(2 + 2, 4); }

// A test you can deliberately break for the demo:
TEST(DemoTest, IntentionallyBreakMe) {
  int kibbles = 100;
  kibbles -= 10;          // spend on a tower
  EXPECT_EQ(kibbles, 90); // change 90 → 91 to demo a red pipeline
}
