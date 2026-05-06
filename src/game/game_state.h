#pragma once

#include <optional>
#include <vector>

#include "game/types.h"

struct GameState {
  std::vector<Enemy> enemies;
  std::vector<Tower> towers;
  std::vector<HitSplat> hit_splats;
  std::vector<Projectile> projectiles;
  std::vector<Shockwave> shockwaves;
  std::vector<Beam> beams;
  std::vector<AreaHighlight> area_highlights;
  std::optional<HeldTower> held_tower;

  int map_index = 0;
  int kibbles = 0;
  int lives = 0;
  int wave = 0;
  bool wave_active = false;
  bool game_over = false;
  bool victory = false;
  int spawn_remaining = 0;
  int spawn_cooldown_ms = 0;
  bool fast_forward = false;
  bool auto_waves = false;

  bool unlocked_thunder = false;
  bool unlocked_fat = false;
  bool unlocked_kitty = false;
  bool unlocked_catatonic = false;
  bool unlocked_galactic = false;

  void Reset(bool dev_mode);
};

inline bool OverlapsTower(const GameState &state, const Position &p, int size) {
  for (const auto &t : state.towers) {
    const int tx1 = t.pos.x;
    const int ty1 = t.pos.y;
    const int tx2 = tx1 + t.size - 1;
    const int ty2 = ty1 + t.size - 1;
    const int px2 = p.x + size - 1;
    const int py2 = p.y + size - 1;
    const bool overlap = !(p.x > tx2 || px2 < tx1 || p.y > ty2 || py2 < ty1);
    if (overlap) {
      return true;
    }
  }
  return false;
}

inline bool IsUnlocked(const GameState &state, Tower::Type type) {
  switch (type) {
  case Tower::Type::Default:
    return true;
  case Tower::Type::Fat:
    return state.unlocked_fat;
  case Tower::Type::Kitty:
    return state.unlocked_kitty;
  case Tower::Type::Thunder:
    return state.unlocked_thunder;
  case Tower::Type::Catatonic:
    return state.unlocked_catatonic;
  case Tower::Type::Galactic:
    return state.unlocked_galactic;
  }
  return true;
}
