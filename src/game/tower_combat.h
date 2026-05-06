#pragma once

#include <functional>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "game/game_state.h"
#include "game/map_path.h"
#include "game/types.h"

class TowerCombat {
public:
  using SfxFn = std::function<void(std::string_view)>;

  TowerCombat(GameState &state, const MapPath &map, std::mt19937 &rng,
              SfxFn sfx);

  void Act(float dt);
  void ReturnKittiesHome();

private:
  GameState &state_;
  const MapPath &map_;
  std::mt19937 &rng_;
  SfxFn sfx_;

  float Rand(float min, float max);
  float NextCooldown(float base_rate);
  void PlayDeathSfx(EnemyType type);

  std::vector<std::vector<bool>>
  TowerOccupancyMaskSkipping(const std::vector<size_t> &skip_indices) const;

  std::optional<size_t> FindTargetAt(const Tower &t, const Vec2 &center) const;
  std::optional<size_t> FindTarget(const Tower &t) const;

  Position NearestOpenCell(const Position &desired,
                           const std::vector<std::vector<bool>> &blocked,
                           std::vector<std::vector<bool>> &reserved,
                           const Position &fallback);

  std::vector<Position> KittyAttackArea(const Vec2 &center,
                                        const Position &target_cell) const;
  bool KittyAreaHitsEnemy(const std::vector<Position> &cells) const;
  bool KittyCellBlocked(
      const Position &p,
      const std::vector<std::vector<bool>> &static_blocked,
      const std::vector<std::vector<bool>> &reserved,
      const std::optional<Position> &ignore_reserved = std::nullopt) const;
  bool CanKittyOccupyCell(size_t kitty_index, const Position &p) const;
  std::optional<Position>
  ChooseKittyLanding(size_t tower_index,
                     const std::vector<std::vector<bool>> &static_blocked,
                     std::vector<std::vector<bool>> &reserved);

  void HandleKittyAttacks();
  std::vector<size_t> ThunderTargets(const Tower &t) const;
  void FireDefault(Tower &t, size_t target_index);
  void FireLaser(const Tower &t, Enemy &target);
  void FireShockwave(const Tower &t);
  void FireKitty(const Tower &t, Enemy &target);
  void FireCatatonic(const Tower &t);
  void FireGalactic(const Tower &t, Enemy &target);
};
