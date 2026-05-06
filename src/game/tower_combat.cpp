#include "game/tower_combat.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

TowerCombat::TowerCombat(GameState &state, const MapPath &map,
                         std::mt19937 &rng, SfxFn sfx)
    : state_(state), map_(map), rng_(rng), sfx_(std::move(sfx)) {}

void TowerCombat::Act(float dt) {
  for (auto &t : state_.towers) {
    t.cooldown -= dt;
  }

  for (size_t i = 0; i < state_.towers.size(); ++i) {
    Tower &t = state_.towers[i];
    if (t.type == Tower::Type::Kitty || t.cooldown > 0.0F) {
      continue;
    }

    const auto target_index = FindTarget(t);
    if (!target_index.has_value()) {
      continue;
    }

    switch (t.type) {
    case Tower::Type::Default:
      FireDefault(t, *target_index);
      sfx_("tower_default_shoot");
      break;
    case Tower::Type::Thunder: {
      const auto targets = ThunderTargets(t);
      if (targets.empty()) {
        continue;
      }
      for (size_t idx : targets) {
        FireLaser(t, state_.enemies[idx]);
      }
      sfx_("tower_thunder_shoot");
      break;
    }
    case Tower::Type::Fat:
      FireShockwave(t);
      sfx_("tower_fat_shoot");
      break;
    case Tower::Type::Catatonic:
      FireCatatonic(t);
      sfx_("tower_catatonic_shoot");
      break;
    case Tower::Type::Galactic:
      FireGalactic(t, state_.enemies[*target_index]);
      sfx_("tower_galactic_shoot");
      break;
    case Tower::Type::Kitty:
      break;
    }
    t.cooldown = NextCooldown(t.fire_rate);
  }

  HandleKittyAttacks();
}

void TowerCombat::ReturnKittiesHome() {
  std::vector<size_t> kitty_indices;
  for (size_t i = 0; i < state_.towers.size(); ++i) {
    if (state_.towers[i].type == Tower::Type::Kitty) {
      kitty_indices.push_back(i);
    }
  }
  if (kitty_indices.empty()) {
    return;
  }

  auto static_blocked = TowerOccupancyMaskSkipping(kitty_indices);
  std::vector<std::vector<bool>> reserved(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));

  for (size_t idx : kitty_indices) {
    Tower &t = state_.towers[idx];
    if (t.pos.x == t.home.x && t.pos.y == t.home.y) {
      reserved[static_cast<size_t>(t.pos.y)][static_cast<size_t>(t.pos.x)] =
          true;
      continue;
    }
    Position dest = NearestOpenCell(t.home, static_blocked, reserved, t.pos);
    t.pos = dest;
  }
}

float TowerCombat::Rand(float min, float max) {
  std::uniform_real_distribution<float> dist(min, max);
  return dist(rng_);
}

float TowerCombat::NextCooldown(float base_rate) {
  const float scaled = base_rate / kSpeedFactor;
  return std::max(0.06F, scaled + Rand(-0.14F, 0.14F));
}

void TowerCombat::PlayDeathSfx(EnemyType type) {
  switch (type) {
  case EnemyType::Mouse:
    sfx_("mouse_die");
    break;
  case EnemyType::Rat:
    sfx_("rat_die");
    break;
  case EnemyType::BigRat:
    sfx_("bigrat_die");
    break;
  case EnemyType::Dog:
    sfx_("dog_die");
    break;
  }
}

std::vector<std::vector<bool>>
TowerCombat::TowerOccupancyMaskSkipping(
    const std::vector<size_t> &skip_indices) const {
  std::vector<bool> skip_lookup(state_.towers.size(), false);
  for (size_t idx : skip_indices) {
    if (idx < skip_lookup.size()) {
      skip_lookup[idx] = true;
    }
  }
  std::vector<std::vector<bool>> mask(kBoardHeight,
                                      std::vector<bool>(kBoardWidth, false));
  for (size_t i = 0; i < state_.towers.size(); ++i) {
    if (skip_lookup[i]) {
      continue;
    }
    const auto &t = state_.towers[i];
    for (int dy = 0; dy < t.size; ++dy) {
      for (int dx = 0; dx < t.size; ++dx) {
        const int cx = t.pos.x + dx;
        const int cy = t.pos.y + dy;
        if (cx < 0 || cy < 0 || cx >= kBoardWidth || cy >= kBoardHeight) {
          continue;
        }
        mask[static_cast<size_t>(cy)][static_cast<size_t>(cx)] = true;
      }
    }
  }
  return mask;
}

std::optional<size_t> TowerCombat::FindTargetAt(const Tower &t,
                                                 const Vec2 &center) const {
  std::optional<size_t> best;
  float best_progress = -1.0F;
  const float range2 = t.range * t.range;

  for (size_t i = 0; i < state_.enemies.size(); ++i) {
    if (state_.enemies[i].hp <= 0) {
      continue;
    }
    const auto pos = map_.EnemyCell(state_.enemies[i]);
    if (t.type != Tower::Type::Thunder) {
      const float d2 = DistanceSquared(center, pos);
      if (d2 > range2) {
        continue;
      }
    }
    if (state_.enemies[i].path_progress > best_progress) {
      best_progress = state_.enemies[i].path_progress;
      best = i;
    }
  }
  return best;
}

std::optional<size_t> TowerCombat::FindTarget(const Tower &t) const {
  return FindTargetAt(t, TowerCenter(t));
}

Position TowerCombat::NearestOpenCell(
    const Position &desired, const std::vector<std::vector<bool>> &blocked,
    std::vector<std::vector<bool>> &reserved, const Position &fallback) {
  std::vector<Position> best;
  float best_d2 = std::numeric_limits<float>::max();
  const Vec2 desired_center = TowerCenterAt(desired, 1);
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      Position p{x, y};
      if (map_.PathMask()[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)]) {
        continue;
      }
      if (blocked[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)]) {
        continue;
      }
      if (reserved[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)]) {
        continue;
      }
      const float d2 = DistanceSquared(desired_center, p);
      if (d2 + 1e-4F < best_d2) {
        best_d2 = d2;
        best.clear();
        best.push_back(p);
      } else if (std::abs(d2 - best_d2) < 1e-4F) {
        best.push_back(p);
      }
    }
  }

  if (best.empty()) {
    if (fallback.x >= 0 && fallback.y >= 0 && fallback.x < kBoardWidth &&
        fallback.y < kBoardHeight) {
      reserved[static_cast<size_t>(fallback.y)]
              [static_cast<size_t>(fallback.x)] = true;
    }
    return fallback;
  }
  std::shuffle(best.begin(), best.end(), rng_);
  const auto chosen = best.front();
  reserved[static_cast<size_t>(chosen.y)][static_cast<size_t>(chosen.x)] =
      true;
  return chosen;
}

std::vector<Position> TowerCombat::KittyAttackArea(
    const Vec2 &center, const Position &target_cell) const {
  const float dx = static_cast<float>(target_cell.x) - center.x;
  const float dy = static_cast<float>(target_cell.y) - center.y;
  const bool horizontal = std::abs(dx) >= std::abs(dy);
  const int primary_x = horizontal ? ((dx > 0) - (dx < 0)) : 0;
  const int primary_y = horizontal ? 0 : ((dy > 0) - (dy < 0));
  const int perp_x = horizontal ? 0 : -primary_y;
  const int perp_y = horizontal ? primary_x : 0;

  std::vector<Position> area_cells;
  for (int step = 1; step <= 3; ++step) {
    for (int off : {-1, 0, 1}) {
      const int gx = static_cast<int>(std::round(center.x)) +
                     primary_x * step + perp_x * off;
      const int gy = static_cast<int>(std::round(center.y)) +
                     primary_y * step + perp_y * off;
      if (gx < 0 || gy < 0 || gx >= kBoardWidth || gy >= kBoardHeight) {
        continue;
      }
      area_cells.push_back({gx, gy});
    }
  }
  return area_cells;
}

bool TowerCombat::KittyAreaHitsEnemy(const std::vector<Position> &cells) const {
  for (const auto &e : state_.enemies) {
    if (e.hp <= 0) {
      continue;
    }
    const auto pos = map_.EnemyCell(e);
    const bool hit =
        std::any_of(cells.begin(), cells.end(), [&](const Position &c) {
          return c.x == pos.x && c.y == pos.y;
        });
    if (hit) {
      return true;
    }
  }
  return false;
}

bool TowerCombat::KittyCellBlocked(
    const Position &p, const std::vector<std::vector<bool>> &static_blocked,
    const std::vector<std::vector<bool>> &reserved,
    const std::optional<Position> &ignore_reserved) const {
  if (p.x < 0 || p.y < 0 || p.x >= kBoardWidth || p.y >= kBoardHeight) {
    return true;
  }
  if (map_.OccupiesPath(p, 1)) {
    return true;
  }
  const bool reserved_here =
      reserved[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)];
  if (reserved_here) {
    if (!ignore_reserved.has_value() || ignore_reserved->x != p.x ||
        ignore_reserved->y != p.y) {
      return true;
    }
  }
  return static_blocked[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)];
}

bool TowerCombat::CanKittyOccupyCell(size_t kitty_index,
                                      const Position &p) const {
  if (p.x < 0 || p.y < 0 || p.x >= kBoardWidth || p.y >= kBoardHeight) {
    return false;
  }
  if (map_.OccupiesPath(p, 1)) {
    return false;
  }
  for (size_t i = 0; i < state_.towers.size(); ++i) {
    if (i == kitty_index) {
      continue;
    }
    const auto &t = state_.towers[i];
    const int tx2 = t.pos.x + t.size - 1;
    const int ty2 = t.pos.y + t.size - 1;
    if (p.x >= t.pos.x && p.x <= tx2 && p.y >= t.pos.y && p.y <= ty2) {
      return false;
    }
  }
  return true;
}

std::optional<Position> TowerCombat::ChooseKittyLanding(
    size_t tower_index, const std::vector<std::vector<bool>> &static_blocked,
    std::vector<std::vector<bool>> &reserved) {
  const Tower &t = state_.towers[tower_index];
  const Vec2 origin = TowerCenter(t);
  const float jump_range = t.range + kKittyJumpBonusRange;
  const float jump_r2 = jump_range * jump_range;

  std::vector<Position> candidates;
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      Position cell{x, y};
      if (KittyCellBlocked(cell, static_blocked, reserved, t.pos)) {
        continue;
      }
      const float d2 = DistanceSquared(origin, cell);
      if (d2 > jump_r2) {
        continue;
      }

      const Vec2 landing_center = TowerCenterAt(cell, t.size);
      const auto target_idx = FindTargetAt(t, landing_center);
      if (!target_idx.has_value()) {
        continue;
      }
      const auto area = KittyAttackArea(
          landing_center, map_.EnemyCell(state_.enemies[*target_idx]));
      if (!KittyAreaHitsEnemy(area)) {
        continue;
      }

      candidates.push_back(cell);
    }
  }

  if (candidates.empty()) {
    const auto yi = static_cast<size_t>(t.pos.y);
    const auto xi = static_cast<size_t>(t.pos.x);
    if (!KittyCellBlocked(t.pos, static_blocked, reserved, t.pos)) {
      reserved[yi][xi] = true;
      return t.pos;
    }
    return std::nullopt;
  }

  std::shuffle(candidates.begin(), candidates.end(), rng_);
  for (const auto &c : candidates) {
    const auto yi = static_cast<size_t>(c.y);
    const auto xi = static_cast<size_t>(c.x);
    if (reserved[yi][xi] && !(c.x == t.pos.x && c.y == t.pos.y)) {
      continue;
    }
    reserved[yi][xi] = true;
    return c;
  }
  return std::nullopt;
}

void TowerCombat::HandleKittyAttacks() {
  std::vector<size_t> ready_kitties;
  for (size_t i = 0; i < state_.towers.size(); ++i) {
    Tower &t = state_.towers[i];
    if (t.type != Tower::Type::Kitty || t.cooldown > 0.0F) {
      continue;
    }
    if (state_.enemies.empty()) {
      continue;
    }
    ready_kitties.push_back(i);
  }
  if (ready_kitties.empty()) {
    return;
  }

  std::vector<size_t> jumping_kitties;
  for (size_t idx : ready_kitties) {
    if (state_.towers[idx].upgraded) {
      jumping_kitties.push_back(idx);
    }
  }

  auto static_blocked = TowerOccupancyMaskSkipping(jumping_kitties);
  std::vector<std::vector<bool>> reserved(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));
  for (size_t idx : jumping_kitties) {
    const auto &p = state_.towers[idx].pos;
    if (p.y >= 0 && p.y < kBoardHeight && p.x >= 0 && p.x < kBoardWidth) {
      reserved[static_cast<size_t>(p.y)][static_cast<size_t>(p.x)] = true;
    }
  }
  std::unordered_map<size_t, Position> planned_landings;

  std::vector<size_t> jump_order = jumping_kitties;
  std::shuffle(jump_order.begin(), jump_order.end(), rng_);
  for (size_t idx : jump_order) {
    auto landing = ChooseKittyLanding(idx, static_blocked, reserved);
    if (landing.has_value()) {
      planned_landings[idx] = *landing;
    }
  }

  for (size_t idx : ready_kitties) {
    Tower &t = state_.towers[idx];
    const Position original = t.pos;
    Position destination = t.pos;
    if (t.upgraded) {
      const auto it = planned_landings.find(idx);
      if (it != planned_landings.end()) {
        destination = it->second;
      }
      const bool destination_changed =
          destination.x != t.pos.x || destination.y != t.pos.y;
      if (destination_changed && !CanKittyOccupyCell(idx, destination)) {
        destination = t.pos;
      }
      if (destination.x != t.pos.x || destination.y != t.pos.y) {
        t.pos = destination;
      }
    }

    const auto target = FindTarget(t);
    if (!target.has_value()) {
      t.pos = original;
      continue;
    }

    FireKitty(t, state_.enemies[*target]);
    sfx_("tower_kitty_shoot");
    t.cooldown = NextCooldown(t.fire_rate);
  }
}

std::vector<size_t> TowerCombat::ThunderTargets(const Tower &t) const {
  std::vector<std::pair<float, size_t>> sorted;
  for (size_t i = 0; i < state_.enemies.size(); ++i) {
    if (state_.enemies[i].hp <= 0) {
      continue;
    }
    sorted.push_back({state_.enemies[i].path_progress, i});
  }
  if (sorted.empty()) {
    return {};
  }
  std::sort(sorted.begin(), sorted.end(),
            [](auto &a, auto &b) { return a.first > b.first; });
  std::vector<size_t> picks;
  auto add_unique = [&](size_t idx) {
    if (std::find(picks.begin(), picks.end(), idx) == picks.end()) {
      picks.push_back(idx);
    }
  };
  add_unique(sorted.front().second);
  if (!t.upgraded) {
    return picks;
  }
  add_unique(sorted[sorted.size() / 2].second);
  add_unique(sorted.back().second);
  return picks;
}

void TowerCombat::FireDefault(Tower &t, size_t target_index) {
  const auto c = TowerCenter(t);
  auto add_projectile = [&](const Enemy &target) {
    Projectile p;
    p.x = static_cast<float>(c.x);
    p.y = static_cast<float>(c.y);
    p.target = map_.EnemyCell(target);
    p.speed = 17.0F;
    p.damage = t.damage;
    state_.projectiles.push_back(p);
  };
  if (t.upgraded) {
    std::vector<std::pair<float, size_t>> sorted;
    const float range2 = t.range * t.range;
    for (size_t j = 0; j < state_.enemies.size(); ++j) {
      if (state_.enemies[j].hp <= 0)
        continue;
      const auto pos = map_.EnemyCell(state_.enemies[j]);
      if (DistanceSquared(c, pos) > range2)
        continue;
      sorted.push_back({state_.enemies[j].path_progress, j});
    }
    if (!sorted.empty()) {
      std::sort(sorted.begin(), sorted.end(),
                [](auto &a, auto &b) { return a.first > b.first; });
      size_t front_idx = sorted.front().second;
      size_t back_idx = sorted.back().second;
      size_t mid_idx = sorted[sorted.size() / 2].second;
      add_projectile(state_.enemies[front_idx]);
      if (mid_idx != front_idx)
        add_projectile(state_.enemies[mid_idx]);
      if (back_idx != front_idx && back_idx != mid_idx)
        add_projectile(state_.enemies[back_idx]);
    }
  } else {
    add_projectile(state_.enemies[target_index]);
  }
}

void TowerCombat::FireLaser(const Tower &t, Enemy &target) {
  const auto center = TowerCenter(t);
  const auto target_cell = map_.EnemyCell(target);
  const float dx = static_cast<float>(target_cell.x) - center.x;
  const float dy = static_cast<float>(target_cell.y) - center.y;
  const float len = std::max(0.001F, std::sqrt(dx * dx + dy * dy));
  const float ndx = dx / len;
  const float ndy = dy / len;

  for (auto &e : state_.enemies) {
    const auto pos = map_.EnemyCell(e);
    const float vx = static_cast<float>(pos.x) - center.x;
    const float vy = static_cast<float>(pos.y) - center.y;
    const float dot = vx * ndx + vy * ndy;
    if (dot < -0.2F) {
      continue;
    }
    const float cross = std::abs(vx * ndy - vy * ndx);
    if (cross <= 0.35F) {
      e.hp -= t.damage;
      if (e.hp <= 0) {
        state_.kibbles += Bounty(e.type);
        PlayDeathSfx(e.type);
      } else {
        state_.hit_splats.push_back({map_.EnemyCell(e), 0.18F});
      }
    }
  }

  Beam b;
  float bx = static_cast<float>(center.x);
  float by = static_cast<float>(center.y);
  for (int i = 0; i < 120; ++i) {
    const int cx = static_cast<int>(std::round(bx));
    const int cy = static_cast<int>(std::round(by));
    if (cx < 0 || cy < 0 || cx >= kBoardWidth || cy >= kBoardHeight) {
      break;
    }
    b.cells.push_back({cx, cy});
    bx += ndx * 0.5F;
    by += ndy * 0.5F;
  }
  state_.beams.push_back(std::move(b));
}

void TowerCombat::FireShockwave(const Tower &t) {
  Shockwave sw;
  sw.center = TowerCenter(t);
  sw.radius = 0.0F;
  sw.max_radius = t.range;
  sw.speed = 10.0F;
  sw.time_left = 0.45F;
  state_.shockwaves.push_back(sw);

  for (auto &e : state_.enemies) {
    const auto pos = map_.EnemyCell(e);
    if (InRange(sw.center, pos, t.range)) {
      e.hp -= t.damage;
      if (e.hp <= 0) {
        state_.kibbles += Bounty(e.type);
        PlayDeathSfx(e.type);
      } else {
        state_.hit_splats.push_back({pos, 0.22F});
      }
    }
  }
}

void TowerCombat::FireKitty(const Tower &t, Enemy &target) {
  const auto center = TowerCenter(t);
  const auto target_cell = map_.EnemyCell(target);
  const auto area_cells = KittyAttackArea(center, target_cell);

  for (auto &e : state_.enemies) {
    const auto pos = map_.EnemyCell(e);
    const bool hit = std::any_of(
        area_cells.begin(), area_cells.end(),
        [&](const Position &c) { return c.x == pos.x && c.y == pos.y; });
    if (!hit) {
      continue;
    }
    e.hp -= t.damage;
    if (e.hp <= 0) {
      state_.kibbles += Bounty(e.type);
      PlayDeathSfx(e.type);
    } else {
      state_.hit_splats.push_back({pos, 0.18F});
    }
  }

  if (!area_cells.empty()) {
    state_.area_highlights.push_back({area_cells, 0.22F});
  }
}

void TowerCombat::FireCatatonic(const Tower &t) {
  const float radius = t.upgraded ? t.range + 0.8F : t.range;
  const float sleep_dur = std::clamp(
      t.upgraded ? kCatSleepUpgrade : kCatSleepBase, 0.0F, kCatSleepCap);
  std::vector<Position> cells;
  for (auto &e : state_.enemies) {
    const auto pos = map_.EnemyCell(e);
    if (InRange(TowerCenter(t), pos, radius)) {
      e.sleep_timer =
          std::min(kCatSleepCap, std::max(e.sleep_timer, sleep_dur));
      cells.push_back(pos);
    }
  }
  if (!cells.empty()) {
    state_.area_highlights.push_back({cells, 0.6F, ftxui::Color::Purple, '~'});
  }
}

void TowerCombat::FireGalactic(const Tower &t, Enemy &target) {
  const auto center = TowerCenter(t);
  const auto target_cell = map_.EnemyCell(target);
  const float dx = static_cast<float>(target_cell.x) - center.x;
  const float dy = static_cast<float>(target_cell.y) - center.y;
  const float len = std::max(0.001F, std::sqrt(dx * dx + dy * dy));
  const float ndx = dx / len;
  const float ndy = dy / len;
  const float range = t.range;
  const float cone_cos = std::cos(0.6F);

  std::vector<Position> cells;
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      const float vx = static_cast<float>(x) - center.x;
      const float vy = static_cast<float>(y) - center.y;
      const float dist2 = vx * vx + vy * vy;
      if (dist2 > range * range)
        continue;
      const float dist = std::sqrt(dist2);
      if (dist < 0.1F)
        continue;
      const float dot = (vx / dist) * ndx + (vy / dist) * ndy;
      if (dot >= cone_cos) {
        cells.push_back({x, y});
      }
    }
  }

  bool void_proc = t.upgraded && Rand(0.0F, 1.0F) < kGalacticVoidChance;
  for (auto &e : state_.enemies) {
    const auto pos = map_.EnemyCell(e);
    const bool hit =
        std::any_of(cells.begin(), cells.end(), [&](const Position &c) {
          return c.x == pos.x && c.y == pos.y;
        });
    if (!hit)
      continue;
    if (void_proc) {
      e.path_progress =
          std::max(0.0F, e.path_progress - kGalacticVoidBackstep);
    }
    e.hp -= t.damage;
    if (e.hp <= 0) {
      state_.kibbles += Bounty(e.type);
      PlayDeathSfx(e.type);
    } else {
      state_.hit_splats.push_back({pos, 0.2F});
    }
  }

  if (!cells.empty()) {
    state_.area_highlights.push_back(
        {cells, void_proc ? 0.35F : 0.3F,
         void_proc ? ftxui::Color::DarkMagenta : ftxui::Color::LightSteelBlue,
         void_proc ? '~' : '*'});
  }
}
