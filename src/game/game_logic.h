#pragma once

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Core enums shared between game logic and tests.
// ---------------------------------------------------------------------------

enum class EnemyType { Mouse, Rat, BigRat, Dog };

// ---------------------------------------------------------------------------
// Enemy progression table — one entry per map (0-9).
// Each field is a [start, end] pair lerped across the 10 waves of that map.
// Remaining weight after mouse + bigrat + dog is assigned to Rat.
// ---------------------------------------------------------------------------

struct MapEnemyWeights {
  float mouse_start,  mouse_end;
  float bigrat_start, bigrat_end;
  float dog_start,    dog_end;
};

constexpr MapEnemyWeights kMapWeights[10] = {
  // map 0: almost all mice, trace rats, no heavies
  {0.95F, 0.78F,  0.00F, 0.00F,  0.00F, 0.00F},
  // map 1: still mostly mice, rats filling the gap
  {0.65F, 0.45F,  0.00F, 0.00F,  0.00F, 0.00F},
  // map 2: mice taper, big rats debut at end
  {0.40F, 0.28F,  0.00F, 0.10F,  0.00F, 0.00F},
  // map 3: rats dominant, big rats growing
  {0.28F, 0.22F,  0.08F, 0.22F,  0.00F, 0.00F},
  // map 4: big rats increasing
  {0.22F, 0.18F,  0.20F, 0.35F,  0.00F, 0.00F},
  // map 5: big rats near majority
  {0.18F, 0.15F,  0.32F, 0.48F,  0.00F, 0.00F},
  // map 6: big rats dominant
  {0.15F, 0.12F,  0.42F, 0.55F,  0.00F, 0.00F},
  // map 7: first trace of dogs at end
  {0.12F, 0.10F,  0.50F, 0.54F,  0.00F, 0.03F},
  // map 8: dogs appear properly
  {0.10F, 0.08F,  0.45F, 0.42F,  0.08F, 0.22F},
  // map 9: dogs are a real threat
  {0.08F, 0.07F,  0.38F, 0.33F,  0.22F, 0.38F},
};

// ---------------------------------------------------------------------------
// Pure game-logic functions — no state, no FTXUI dependency.
// ---------------------------------------------------------------------------

// Difficulty level at a given (wave, map_index). Resets to 1-10 each map,
// plus a permanent map bonus so later maps stay harder.
inline int DifficultyLevel(int wave, int map_index) {
  const int local = (wave - 1) % 10 + 1;
  return local + map_index * 2;
}

// Kibbles awarded at the end of a wave. Local wave resets per map so late-game
// income stays controlled.
inline int WaveCompletionBonus(int wave) {
  return 9 + ((wave - 1) % 10 + 1);
}

// Number of enemies spawned in a wave at the given difficulty.
inline int SpawnCount(int diff) {
  return 6 + diff;
}

// Max HP of an enemy at a given difficulty level.
inline int EnemyMaxHP(EnemyType type, int diff) {
  const float f = static_cast<float>(diff);
  switch (type) {
  case EnemyType::Mouse:  return static_cast<int>((2.0F + f)          * 1.2F);
  case EnemyType::Rat:    return static_cast<int>((5.0F + f * 2.5F)   * 1.2F);
  case EnemyType::BigRat: return static_cast<int>((15.0F + f * 4.0F)  * 1.2F);
  case EnemyType::Dog:    return static_cast<int>((28.0F + f * 6.0F)  * 1.2F);
  }
  return 1;
}

// Kill reward by enemy type.
inline int EnemyBounty(EnemyType type) {
  switch (type) {
  case EnemyType::Mouse:  return 4;
  case EnemyType::Rat:    return 5;
  case EnemyType::BigRat: return 9;
  case EnemyType::Dog:    return 13;
  }
  return 5;
}

// Kibbles returned when selling a tower (20% of original cost).
inline int SellRefund(int cost) {
  return static_cast<int>(std::round(static_cast<float>(cost) * 0.25F));
}

// Determine enemy type from a pre-supplied uniform random roll in [0, 1).
// Separating the RNG call makes this testable without seeding.
inline EnemyType EnemyTypeForRoll(int map_idx, int wave, float roll) {
  map_idx = std::clamp(map_idx, 0, 9);
  const MapEnemyWeights &w = kMapWeights[map_idx];
  const float t = static_cast<float>((wave - 1) % 10) / 9.0F;
  const float mouse_w  = w.mouse_start  + t * (w.mouse_end  - w.mouse_start);
  const float bigrat_w = w.bigrat_start + t * (w.bigrat_end - w.bigrat_start);
  const float dog_w    = w.dog_start    + t * (w.dog_end    - w.dog_start);
  if (roll < mouse_w)                          return EnemyType::Mouse;
  if (roll < mouse_w + bigrat_w)               return EnemyType::BigRat;
  if (roll < mouse_w + bigrat_w + dog_w)       return EnemyType::Dog;
  return EnemyType::Rat;
}
