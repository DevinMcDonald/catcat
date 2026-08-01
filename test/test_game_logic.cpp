#include <gtest/gtest.h>

#include "game/game_logic.h"
#include "version/update_checker.h"

// ── DifficultyLevel ──────────────────────────────────────────────────────────

TEST(DifficultyLevel, FirstWaveFirstMap) {
  EXPECT_EQ(DifficultyLevel(1, 0), 1);
}

TEST(DifficultyLevel, LastWaveFirstMap) {
  EXPECT_EQ(DifficultyLevel(10, 0), 10);
}

TEST(DifficultyLevel, LocalWaveResetsEachMap) {
  // wave 11 on map 1: local=(11-1)%10+1=1, map bonus=2, total=3
  EXPECT_EQ(DifficultyLevel(11, 1), 3);
  // wave 21 on map 2: local=1, map bonus=4, total=5
  EXPECT_EQ(DifficultyLevel(21, 2), 5);
}

TEST(DifficultyLevel, MapBonusScalesCorrectly) {
  // same local wave (wave=1), different maps differ by exactly 2 per map level
  for (int m = 0; m < 9; ++m) {
    EXPECT_EQ(DifficultyLevel(1, m + 1) - DifficultyLevel(1, m), 2);
  }
}

TEST(DifficultyLevel, MaxAtLastMapLastWave) {
  // map 9 wave 10: local=10, bonus=18, total=28
  EXPECT_EQ(DifficultyLevel(10, 9), 28);
}

TEST(DifficultyLevel, NeverBelowOne) {
  EXPECT_GE(DifficultyLevel(1, 0), 1);
}

// ── WaveCompletionBonus ───────────────────────────────────────────────────────

TEST(WaveCompletionBonus, FirstWave) {
  // local=1 → 9+1=10
  EXPECT_EQ(WaveCompletionBonus(1), 10);
}

TEST(WaveCompletionBonus, TenthWave) {
  // local=10 → 9+10=19
  EXPECT_EQ(WaveCompletionBonus(10), 19);
}

TEST(WaveCompletionBonus, ResetsOnNewMap) {
  // wave 11 is the first wave of map 2 — same bonus as wave 1
  EXPECT_EQ(WaveCompletionBonus(11), WaveCompletionBonus(1));
  EXPECT_EQ(WaveCompletionBonus(21), WaveCompletionBonus(1));
}

TEST(WaveCompletionBonus, ClampedBetween10And19) {
  for (int wave = 1; wave <= 100; ++wave) {
    EXPECT_GE(WaveCompletionBonus(wave), 10);
    EXPECT_LE(WaveCompletionBonus(wave), 19);
  }
}

TEST(WaveCompletionBonus, GrowsWithinEachMap) {
  // Bonus strictly increases wave-over-wave within each 10-wave block
  for (int base = 1; base <= 91; base += 10) {
    for (int local = 1; local < 10; ++local) {
      EXPECT_LT(WaveCompletionBonus(base + local - 1),
                WaveCompletionBonus(base + local));
    }
  }
}

// ── SpawnCount ───────────────────────────────────────────────────────────────

TEST(SpawnCount, BasePlusDiff) {
  EXPECT_EQ(SpawnCount(1), 7);
  EXPECT_EQ(SpawnCount(10), 16);
  EXPECT_EQ(SpawnCount(28), 34); // map 9 last wave max diff
}

TEST(SpawnCount, StrictlyIncreasing) {
  for (int d = 1; d < 30; ++d) {
    EXPECT_EQ(SpawnCount(d + 1) - SpawnCount(d), 1);
  }
}

// ── EnemyMaxHP ───────────────────────────────────────────────────────────────

TEST(EnemyMaxHP, KnownValuesAtDiff1) {
  // Mouse:  (2.0+1)*1.2  = 3.6  → 3
  EXPECT_EQ(EnemyMaxHP(EnemyType::Mouse,  1), 3);
  // Rat:    (5+2.5)*1.2  = 9.0  → 9
  EXPECT_EQ(EnemyMaxHP(EnemyType::Rat,    1), 9);
  // BigRat: (15+4)*1.2   = 22.8 → 22
  EXPECT_EQ(EnemyMaxHP(EnemyType::BigRat, 1), 22);
  // Dog:    (28+6)*1.2   = 40.8 → 40
  EXPECT_EQ(EnemyMaxHP(EnemyType::Dog,    1), 40);
}

TEST(EnemyMaxHP, KnownValuesAtDiff10) {
  // Rat: (5+25)*1.2 = 36
  EXPECT_EQ(EnemyMaxHP(EnemyType::Rat, 10), 36);
  // Dog: (28+60)*1.2 = 105.6 → 105
  EXPECT_EQ(EnemyMaxHP(EnemyType::Dog, 10), 105);
}

TEST(EnemyMaxHP, HardnessOrderHoldsAtAllDifficulties) {
  for (int d : {1, 5, 10, 20, 28}) {
    EXPECT_LT(EnemyMaxHP(EnemyType::Mouse,  d), EnemyMaxHP(EnemyType::Rat,    d));
    EXPECT_LT(EnemyMaxHP(EnemyType::Rat,    d), EnemyMaxHP(EnemyType::BigRat, d));
    EXPECT_LT(EnemyMaxHP(EnemyType::BigRat, d), EnemyMaxHP(EnemyType::Dog,    d));
  }
}

TEST(EnemyMaxHP, NeverBelowOne) {
  for (auto type : {EnemyType::Mouse, EnemyType::Rat, EnemyType::BigRat, EnemyType::Dog}) {
    EXPECT_GE(EnemyMaxHP(type, 1), 1);
  }
}

TEST(EnemyMaxHP, GrowsWithDifficulty) {
  for (auto type : {EnemyType::Mouse, EnemyType::Rat, EnemyType::BigRat, EnemyType::Dog}) {
    EXPECT_LT(EnemyMaxHP(type, 1), EnemyMaxHP(type, 10));
    EXPECT_LT(EnemyMaxHP(type, 10), EnemyMaxHP(type, 28));
  }
}

// ── EnemyBounty ──────────────────────────────────────────────────────────────

TEST(EnemyBounty, ExactValues) {
  EXPECT_EQ(EnemyBounty(EnemyType::Mouse),  4);
  EXPECT_EQ(EnemyBounty(EnemyType::Rat),    5);
  EXPECT_EQ(EnemyBounty(EnemyType::BigRat), 9);
  EXPECT_EQ(EnemyBounty(EnemyType::Dog),   13);
}

TEST(EnemyBounty, HarderEnemiesPayMore) {
  EXPECT_LT(EnemyBounty(EnemyType::Mouse),  EnemyBounty(EnemyType::Rat));
  EXPECT_LT(EnemyBounty(EnemyType::Rat),    EnemyBounty(EnemyType::BigRat));
  EXPECT_LT(EnemyBounty(EnemyType::BigRat), EnemyBounty(EnemyType::Dog));
}

// ── SellRefund ───────────────────────────────────────────────────────────────

TEST(SellRefund, TwentyFivePercentExact) {
  EXPECT_EQ(SellRefund(100), 25);
  EXPECT_EQ(SellRefund(200), 50);
}

TEST(SellRefund, AllTowerCosts) {
  EXPECT_EQ(SellRefund(35),  9);  // Default Cat / Fat Cat: round(35*0.25)=9
  EXPECT_EQ(SellRefund(50),  13); // Kitty Cat: round(50*0.25)=13
  EXPECT_EQ(SellRefund(100), 25); // Thundercat
  EXPECT_EQ(SellRefund(150), 38); // Catatonic: round(150*0.25)=38
  EXPECT_EQ(SellRefund(200), 50); // Galacticat
}

TEST(SellRefund, NeverNegative) {
  EXPECT_GE(SellRefund(0), 0);
  EXPECT_GE(SellRefund(1), 0);
}

// ── EnemyTypeForRoll ─────────────────────────────────────────────────────────

TEST(EnemyTypeForRoll, Map0Wave1AlmostAllMice) {
  // mouse_w = 0.95 on map 0 wave 1
  EXPECT_EQ(EnemyTypeForRoll(0, 1, 0.00F), EnemyType::Mouse);
  EXPECT_EQ(EnemyTypeForRoll(0, 1, 0.50F), EnemyType::Mouse);
  EXPECT_EQ(EnemyTypeForRoll(0, 1, 0.94F), EnemyType::Mouse);
  EXPECT_EQ(EnemyTypeForRoll(0, 1, 0.96F), EnemyType::Rat);
}

TEST(EnemyTypeForRoll, Map0NoBigRatsOrDogs) {
  // bigrat_w = dog_w = 0 across all waves on map 0
  for (int wave = 1; wave <= 10; ++wave) {
    for (int i = 0; i <= 20; ++i) {
      const float roll = static_cast<float>(i) / 20.0F;
      const auto t = EnemyTypeForRoll(0, wave, roll);
      EXPECT_NE(t, EnemyType::BigRat) << "map=0 wave=" << wave << " roll=" << roll;
      EXPECT_NE(t, EnemyType::Dog)    << "map=0 wave=" << wave << " roll=" << roll;
    }
  }
}

TEST(EnemyTypeForRoll, DogsAbsentOnMaps0Through6) {
  for (int map = 0; map <= 6; ++map) {
    for (int wave = 1; wave <= 10; ++wave) {
      // dog_w = 0 on these maps, so no roll can produce Dog
      for (int i = 0; i <= 20; ++i) {
        const float roll = static_cast<float>(i) / 20.0F;
        EXPECT_NE(EnemyTypeForRoll(map, wave, roll), EnemyType::Dog)
            << "map=" << map << " wave=" << wave << " roll=" << roll;
      }
    }
  }
}

TEST(EnemyTypeForRoll, DogsAppearOnMap8) {
  // Map 8 wave 10: mouse_w=0.08, bigrat_w=0.42, dog_w=0.22
  // roll in [0.50, 0.72) → Dog
  EXPECT_EQ(EnemyTypeForRoll(8, 10, 0.55F), EnemyType::Dog);
  EXPECT_EQ(EnemyTypeForRoll(8, 10, 0.70F), EnemyType::Dog);
}

TEST(EnemyTypeForRoll, DogsAppearOnMap7AtEndOnly) {
  // Map 7 wave 1: dog_w=0, wave 10: dog_w=0.03
  // At wave 1, dog_w = 0 so no dog possible
  for (int i = 0; i <= 20; ++i) {
    const float roll = static_cast<float>(i) / 20.0F;
    EXPECT_NE(EnemyTypeForRoll(7, 1, roll), EnemyType::Dog)
        << "Expected no dogs at map 7 wave 1, roll=" << roll;
  }
  // At wave 10, dog_w = 0.03 — narrow but non-zero band exists
  bool found_dog = false;
  for (int i = 0; i <= 100; ++i) {
    if (EnemyTypeForRoll(7, 10, static_cast<float>(i) / 100.0F) == EnemyType::Dog) {
      found_dog = true;
      break;
    }
  }
  EXPECT_TRUE(found_dog) << "Expected dogs to appear at map 7 wave 10";
}

TEST(EnemyTypeForRoll, Map9HasMoreDogsThanMap8) {
  int dogs_map8 = 0, dogs_map9 = 0;
  for (int i = 0; i <= 100; ++i) {
    const float roll = static_cast<float>(i) / 100.0F;
    if (EnemyTypeForRoll(8, 10, roll) == EnemyType::Dog) ++dogs_map8;
    if (EnemyTypeForRoll(9, 10, roll) == EnemyType::Dog) ++dogs_map9;
  }
  EXPECT_GT(dogs_map9, dogs_map8);
}

TEST(EnemyTypeForRoll, HeaviesIncreaseAcrossWavesOnLaterMaps) {
  // On map 5, BigRat weight grows from wave 1 (0.32) to wave 10 (0.48)
  int heavies_early = 0, heavies_late = 0;
  for (int i = 0; i <= 100; ++i) {
    const float roll = static_cast<float>(i) / 100.0F;
    if (EnemyTypeForRoll(5, 1,  roll) == EnemyType::BigRat) ++heavies_early;
    if (EnemyTypeForRoll(5, 10, roll) == EnemyType::BigRat) ++heavies_late;
  }
  EXPECT_GT(heavies_late, heavies_early);
}

TEST(EnemyTypeForRoll, MiceDecreaseAcrossMaps) {
  // At the same wave position (wave 1, t=0), mouse_start decreases each map
  int mice_map0 = 0, mice_map5 = 0, mice_map9 = 0;
  for (int i = 0; i <= 100; ++i) {
    const float roll = static_cast<float>(i) / 100.0F;
    if (EnemyTypeForRoll(0, 1, roll) == EnemyType::Mouse) ++mice_map0;
    if (EnemyTypeForRoll(5, 1, roll) == EnemyType::Mouse) ++mice_map5;
    if (EnemyTypeForRoll(9, 1, roll) == EnemyType::Mouse) ++mice_map9;
  }
  EXPECT_GT(mice_map0, mice_map5);
  EXPECT_GT(mice_map5, mice_map9);
}

TEST(EnemyTypeForRoll, MapIndexClamped) {
  // Negative map treated as map 0
  EXPECT_EQ(EnemyTypeForRoll(-1, 1, 0.0F), EnemyTypeForRoll(0, 1, 0.0F));
  // Map > 9 treated as map 9
  EXPECT_EQ(EnemyTypeForRoll(15, 5, 0.5F), EnemyTypeForRoll(9, 5, 0.5F));
}

TEST(EnemyTypeForRoll, WeightsSumToAtMostOne) {
  // For every map/wave combo, roll=0.9999 must return a valid type (no out-of-range)
  for (int map = 0; map <= 9; ++map) {
    for (int wave = 1; wave <= 10; ++wave) {
      const auto t = EnemyTypeForRoll(map, wave, 0.9999F);
      EXPECT_TRUE(t == EnemyType::Mouse || t == EnemyType::Rat ||
                  t == EnemyType::BigRat || t == EnemyType::Dog)
          << "Unexpected type at map=" << map << " wave=" << wave;
    }
  }
}

// ── NormalizeVersion ─────────────────────────────────────────────────────────

TEST(NormalizeVersion, StripsLowercaseV) {
  EXPECT_EQ(NormalizeVersion("v1.2.3"), "1.2.3");
}

TEST(NormalizeVersion, StripsUppercaseV) {
  EXPECT_EQ(NormalizeVersion("V1.2.3"), "1.2.3");
}

TEST(NormalizeVersion, PassesThroughCleanSemver) {
  EXPECT_EQ(NormalizeVersion("1.2.3"), "1.2.3");
}

TEST(NormalizeVersion, EmptyStringUnchanged) {
  EXPECT_EQ(NormalizeVersion(""), "");
}

TEST(NormalizeVersion, PreservesGitDescribeSuffix) {
  // git describe can produce e.g. "v1.3.9-3-gabcdef"
  EXPECT_EQ(NormalizeVersion("v1.3.9-3-gabcdef"), "1.3.9-3-gabcdef");
}
