#include "game/game_state.h"

void GameState::Reset(bool dev_mode) {
  enemies.clear();
  towers.clear();
  hit_splats.clear();
  projectiles.clear();
  shockwaves.clear();
  beams.clear();
  area_highlights.clear();
  held_tower.reset();

  map_index = 0;
  kibbles = dev_mode ? 1000000 : kStartingKibbles;
  lives = kStartingLives;
  wave = 0;
  wave_active = false;
  game_over = false;
  victory = false;
  spawn_remaining = 0;
  spawn_cooldown_ms = 0;
  fast_forward = false;
  auto_waves = false;

  unlocked_thunder = unlocked_fat = unlocked_kitty = false;
  unlocked_catatonic = unlocked_galactic = false;
  if (dev_mode) {
    unlocked_thunder = unlocked_fat = unlocked_kitty = true;
    unlocked_catatonic = unlocked_galactic = true;
  }
}
