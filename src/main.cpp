#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "audio.hpp"

using namespace std::chrono_literals;
using ftxui::bgcolor;
using ftxui::bold;
using ftxui::border;
using ftxui::color;
using ftxui::hbox;
using ftxui::inverted;
using ftxui::separator;
using ftxui::text;
using ftxui::vbox;

namespace {

constexpr int kBoardWidth = 48;
constexpr int kBoardHeight = 28;
constexpr int kTickMs = 16;  // ~60 FPS
constexpr float kTickSeconds = kTickMs / 1000.0F;
constexpr int kStartingKibbles = 90;
constexpr float kSpeedFactor = 1.3F;  // Global pacing multiplier (~30% faster).
constexpr float kFastForwardMultiplier = 5.0F;
constexpr int kStartingLives = 9;
constexpr float kCatSleepBase = 0.5F;
constexpr float kCatSleepUpgrade = 1.0F;
constexpr float kCatSleepCap = 5.0F;
constexpr float kGalacticVoidChance = 0.20F;
constexpr float kGalacticVoidBackstep = 6.0F;
constexpr float kKittyJumpBonusRange = 1.5F; // extra reach for upgraded jumps

struct Position {
  int x = 0;
  int y = 0;
};

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

float DistanceSquared(const Vec2 &a, const Position &b) {
  const float dx = a.x - static_cast<float>(b.x);
  const float dy = a.y - static_cast<float>(b.y);
  return dx * dx + dy * dy;
}

ftxui::Color BlendColor(const ftxui::Color &base, const ftxui::Color &overlay,
                        float alpha) {
  return ftxui::Color::Interpolate(alpha, base, overlay);
}

enum class EnemyType { Mouse, Rat, BigRat, Dog };

struct Enemy {
  float path_progress = 0.0F; // index along path cells
  float speed = 1.0F;         // cells per second
  int hp = 1;
  int max_hp = 1;
  int lane_offset = 0; // lateral offset from center path
  EnemyType type = EnemyType::Rat;
  float sleep_timer = 0.0F;
};

struct Tower {
  enum class Type { Default, Fat, Kitty, Thunder, Catatonic, Galactic };

  Position pos{};
  int damage = 2;
  float range = 3.2F;
  float cooldown = 0.0F;  // time until next shot
  float fire_rate = 1.2F; // seconds between shots
  Type type = Type::Default;
  int size = 1; // 1x1 or 2x2 for Fat
  bool upgraded = false;
};

struct HitSplat {
  Position pos{};
  float time_left = 0.25F; // seconds
};

struct Projectile {
  float x = 0.0F;
  float y = 0.0F;
  Position target{};
  float speed = 17.0F; // cells per second
  int damage = 0;
};

struct Shockwave {
  Vec2 center{};
  float radius = 0.0F;
  float max_radius = 0.0F;
  float speed = 10.0F;
  float time_left = 0.4F;
  float max_time = 0.4F;
};

struct Beam {
  std::vector<Position> cells;
  float time_left = 0.18F;
};

struct HeldTower {
  Tower tower;
  Position original{};
};

struct AreaHighlight {
  std::vector<Position> cells;
  float time_left = 0.2F;
  ftxui::Color color = ftxui::Color::Pink1;
  char glyph = '#';
};

struct TowerDef {
  Tower::Type type;
  std::string name;
  int cost;
  int damage;
  float range;
  float fire_rate;
  bool show_range;
  int size;
};

struct MapDef {
  std::vector<Position> anchors;
  int path_width = 1;
  ftxui::Color background = ftxui::Color::DarkGreen;
  ftxui::Color path_color = ftxui::Color::DarkGoldenrod;
};

TowerDef GetDef(Tower::Type type) {
  switch (type) {
    case Tower::Type::Default:
      return {type, "Default Cat", 35, 3, 4.5F, 0.85F, true, 1};
  case Tower::Type::Fat:
    return {type, "Fat Cat", 55, 4, 2.4F, 1.4F, true, 2};
  case Tower::Type::Kitty:
    return {type, "Kitty Cat", 100, 3, 3.0F, 1.0F, true, 1};
  case Tower::Type::Thunder:
    return {type, "Thundercat", 350, 6, 999.0F, 2.6F, false, 1};
  case Tower::Type::Catatonic:
    return {type, "Catatonic", 500, 2, 3.2F, 2.2F, true, 1};
  case Tower::Type::Galactic:
    return {type, "Galacticat", 1000, 9, 7.5F, 2.5F, false, 1};
  }
  return {Tower::Type::Default, "Default Cat", 35, 3, 3.5F, 0.85F, true, 1};
}

class Game {
public:
  explicit Game(bool dev_mode = false) : dev_mode_(dev_mode) {
    BuildMaps();
    BuildPath();
    kibbles_ = dev_mode_ ? 1000000 : kStartingKibbles;
    lives_ = kStartingLives;
    cursor_ = {3, kBoardHeight / 2};
    if (dev_mode_) {
      unlocked_thunder_ = unlocked_fat_ = unlocked_kitty_ = true;
      unlocked_catatonic_ = unlocked_galactic_ = true;
    }
#ifdef ENABLE_AUDIO
    audio_ = std::make_unique<AudioSystem>();
    audio_->Init("audio.json");
    audio_->SetMusicForMap(map_index_);
#endif
  }

  std::vector<std::vector<bool>>
  TowerOccupancyMaskSkipping(const std::vector<size_t> &skip_indices) const {
    std::vector<bool> skip_lookup(towers_.size(), false);
    for (size_t idx : skip_indices) {
      if (idx < skip_lookup.size()) {
        skip_lookup[idx] = true;
      }
    }
    std::vector<std::vector<bool>> mask(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    for (size_t i = 0; i < towers_.size(); ++i) {
      if (skip_lookup[i]) {
        continue;
      }
      const auto &t = towers_[i];
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

  std::vector<Position> KittyAttackArea(const Vec2 &center,
                                        const Position &target_cell) const {
    const float dx = static_cast<float>(target_cell.x) - center.x;
    const float dy = static_cast<float>(target_cell.y) - center.y;
    const bool horizontal = std::abs(dx) >= std::abs(dy);
    const int primary_x = horizontal ? ((dx > 0) - (dx < 0)) : 0;
    const int primary_y = horizontal ? 0 : ((dy > 0) - (dy < 0));
    const int perp_x = horizontal ? 0 : -primary_y;
    const int perp_y = horizontal ? primary_x : 0;

    std::vector<Position> area_cells;
    for (int step = 1; step <= 3; ++step) {      // depth 3
      for (int off = -1; off <= 0; ++off) {      // width 2
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

  bool KittyAreaHitsEnemy(const std::vector<Position> &cells) const {
    for (const auto &e : enemies_) {
      if (e.hp <= 0) {
        continue;
      }
      const auto pos = EnemyCell(e);
      const bool hit = std::any_of(
          cells.begin(), cells.end(),
          [&](const Position &c) { return c.x == pos.x && c.y == pos.y; });
      if (hit) {
        return true;
      }
    }
    return false;
  }

  bool KittyCellBlocked(
      const Position &p, const std::vector<std::vector<bool>> &static_blocked,
      const std::vector<std::vector<bool>> &reserved,
      const std::optional<Position> &ignore_reserved = std::nullopt) const {
    if (p.x < 0 || p.y < 0 || p.x >= kBoardWidth || p.y >= kBoardHeight) {
      return true;
    }
    if (OccupiesPath(p, 1)) {
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

  bool CanKittyOccupyCell(size_t kitty_index, const Position &p) const {
    if (p.x < 0 || p.y < 0 || p.x >= kBoardWidth || p.y >= kBoardHeight) {
      return false;
    }
    if (OccupiesPath(p, 1)) {
      return false;
    }
    for (size_t i = 0; i < towers_.size(); ++i) {
      if (i == kitty_index) {
        continue;
      }
      const auto &t = towers_[i];
      const int tx2 = t.pos.x + t.size - 1;
      const int ty2 = t.pos.y + t.size - 1;
      if (p.x >= t.pos.x && p.x <= tx2 && p.y >= t.pos.y && p.y <= ty2) {
        return false;
      }
    }
    return true;
  }

  std::optional<Position> ChooseKittyLanding(
      size_t tower_index, const std::vector<std::vector<bool>> &static_blocked,
      std::vector<std::vector<bool>> &reserved) {
    const Tower &t = towers_[tower_index];
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
        const auto area =
            KittyAttackArea(landing_center, EnemyCell(enemies_[*target_idx]));
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

  void HandleKittyAttacks() {
    std::vector<size_t> ready_kitties;
    for (size_t i = 0; i < towers_.size(); ++i) {
      Tower &t = towers_[i];
      if (t.type != Tower::Type::Kitty || t.cooldown > 0.0F) {
        continue;
      }
      if (enemies_.empty()) {
        continue;
      }
      ready_kitties.push_back(i);
    }
    if (ready_kitties.empty()) {
      return;
    }

    std::vector<size_t> jumping_kitties;
    for (size_t idx : ready_kitties) {
      if (towers_[idx].upgraded) {
        jumping_kitties.push_back(idx);
      }
    }

    auto static_blocked = TowerOccupancyMaskSkipping(jumping_kitties);
    std::vector<std::vector<bool>> reserved(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    for (size_t idx : jumping_kitties) {
      const auto &p = towers_[idx].pos;
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
      Tower &t = towers_[idx];
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

      FireKitty(t, enemies_[*target]);
#ifdef ENABLE_AUDIO
      audio_->PlayEvent("tower_kitty_shoot");
#endif
      t.cooldown = NextCooldown(t.fire_rate);
    }
  }

  void Tick() {
#ifdef ENABLE_AUDIO
    if (audio_)
      audio_->Update();
#endif
    if (game_over_) {
      return;
    }

    SpawnTick();
    MoveEnemies();
    TowersAct();
    MoveProjectiles();
    ResolveProjectiles();
    UpdateShockwaves();
    UpdateBeams();
    UpdateAreas();
    Cleanup();
    UpdateHitSplats();
    CheckWaveCompletion();
    if (lives_ <= 0) {
      if (!game_over_) {
        game_over_ = true;
        auto_waves_ = false;
        SetMusic(-1);
      }
    }
  }

  bool HandleEvent(const ftxui::Event &event) {
    if (game_over_) {
      return false;
    }

    if (event == ftxui::Event::Character('h')) {
      show_controls_ = !show_controls_;
      return true;
    }

    const auto move_cursor = [&](int dx, int dy) {
      cursor_.x = std::clamp(cursor_.x + dx, 0, kBoardWidth - 1);
      cursor_.y = std::clamp(cursor_.y + dy, 0, kBoardHeight - 1);
    };

    bool handled = false;

    if (event == ftxui::Event::ArrowUp ||
        event == ftxui::Event::Character('w')) {
      move_cursor(0, -1);
      handled = true;
    }
    if (event == ftxui::Event::ArrowDown ||
        event == ftxui::Event::Character('s')) {
      move_cursor(0, 1);
      handled = true;
    }
    if (event == ftxui::Event::ArrowLeft ||
        event == ftxui::Event::Character('a')) {
      move_cursor(-1, 0);
      handled = true;
    }
    if (event == ftxui::Event::ArrowRight ||
        event == ftxui::Event::Character('d')) {
      move_cursor(1, 0);
      handled = true;
    }

    if (event == ftxui::Event::Character('c') ||
        event == ftxui::Event::Character(' ')) {
      PlaceTower();
      handled = true;
    }

    if (event == ftxui::Event::Character('n')) {
      auto_waves_ = false;
      StartWave();
      handled = true;
    }
    if (event == ftxui::Event::Character('N')) {
      auto_waves_ = true;
      if (!wave_active_) {
        StartWave();
      }
      handled = true;
    }
    if (event == ftxui::Event::Character('f')) {
      fast_forward_ = !fast_forward_;
      handled = true;
    }

    if (event == ftxui::Event::Character('1')) {
      selected_type_ = Tower::Type::Default;
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('2')) {
      TryUnlockOrSelect(Tower::Type::Fat);
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('3')) {
      TryUnlockOrSelect(Tower::Type::Kitty);
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('4')) {
      TryUnlockOrSelect(Tower::Type::Thunder);
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('5')) {
      TryUnlockOrSelect(Tower::Type::Catatonic);
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('6')) {
      TryUnlockOrSelect(Tower::Type::Galactic);
      overlay_enabled_ = true;
      handled = true;
    }
    if (event == ftxui::Event::Character('p')) {
      view_shop_ = !view_shop_;
      handled = true;
    }
    if (event == ftxui::Event::Character('t')) { // toggle sfx
#ifdef ENABLE_AUDIO
      audio_->ToggleSfx();
#endif
      handled = true;
    }
    if (event == ftxui::Event::Character('y')) { // toggle music
#ifdef ENABLE_AUDIO
      audio_->ToggleMusic();
      if (audio_->MusicEnabled()) {
        audio_->SetMusicForMap(map_index_);
      }
#endif
      handled = true;
    }
    if (event == ftxui::Event::Escape) {
      view_shop_ = false;
      show_controls_ = false;
      if (held_tower_) {
        CancelHold();
        overlay_enabled_ = false;
      } else {
        overlay_enabled_ = false;
      }
      handled = true;
    }
    if (event == ftxui::Event::Character('m')) {
      if (held_tower_) {
        TryPlaceHeld();
      } else {
        PickUpTower();
      }
      handled = true;
    }
    if (event == ftxui::Event::Character('u')) {
      UpgradeTowerAtCursor();
      handled = true;
    }
    if (event == ftxui::Event::Character('x')) {
      SellTowerAtCursor();
      handled = true;
    }

    if (dev_mode_ && event == ftxui::Event::Character('>')) {
      AdvanceMap(true);
      handled = true;
    }

    if (handled && event != ftxui::Event::Custom) {
      show_controls_ = false;
    }
    return handled;
  }

  ftxui::Element Render() const {
    auto board = RenderBoard();
    if (game_over_) {
      auto big_letters =
          ftxui::vbox({ftxui::text("┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼"),
                       ftxui::text("███▀▀▀██┼███▀▀▀███┼███▀█▄█▀███┼██▀▀▀"),
                       ftxui::text("██┼┼┼┼██┼██┼┼┼┼┼██┼██┼┼┼█┼┼┼██┼██┼┼┼"),
                       ftxui::text("██┼┼┼▄▄▄┼██▄▄▄▄▄██┼██┼┼┼▀┼┼┼██┼██▀▀▀"),
                       ftxui::text("██┼┼┼┼██┼██┼┼┼┼┼██┼██┼┼┼┼┼┼┼██┼██┼┼┼"),
                       ftxui::text("███▄▄▄██┼██┼┼┼┼┼██┼██┼┼┼┼┼┼┼██┼██▄▄▄"),
                       ftxui::text("┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼"),
                       ftxui::text("███▀▀▀███┼▀███┼┼██▀┼██▀▀▀┼██▀▀▀▀██▄┼"),
                       ftxui::text("██┼┼┼┼┼██┼┼┼██┼┼██┼┼██┼┼┼┼██┼┼┼┼┼██┼"),
                       ftxui::text("██┼┼┼┼┼██┼┼┼██┼┼██┼┼██▀▀▀┼██▄▄▄▄▄▀▀┼"),
                       ftxui::text("██┼┼┼┼┼██┼┼┼██┼┼█▀┼┼██┼┼┼┼██┼┼┼┼┼██┼"),
                       ftxui::text("███▄▄▄███┼┼┼─▀█▀┼┼─┼██▄▄▄┼██┼┼┼┼┼██▄"),
                       ftxui::text("┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼")}) |
          color(ftxui::Color::RedLight) | bgcolor(ftxui::Color::Black) | bold |
          ftxui::center;
      auto overlay = ftxui::center(ftxui::vbox({
                                        ftxui::filler(),
                                        big_letters | border | bgcolor(ftxui::Color::Black),
                                        ftxui::filler(),
                                    }) |
                                    ftxui::center);
      board = ftxui::dbox({board, overlay});
    }
    auto stats = RenderStats();
    return hbox({
        board | border,
        separator(),
        stats | border,
    });
  }

  bool GameOver() const { return game_over_; }

private:
  void BuildPath() {
    const MapDef &map = CurrentMap();
    path_.clear();
    // Build center path.
    for (size_t i = 1; i < map.anchors.size(); ++i) {
      const auto &from = map.anchors[i - 1];
      const auto &to = map.anchors[i];
      if (from.x == to.x) {
        const int dir = (to.y > from.y) ? 1 : -1;
        for (int y = from.y; y != to.y + dir; y += dir) {
          path_.push_back({from.x, y});
        }
      } else if (from.y == to.y) {
        const int dir = (to.x > from.x) ? 1 : -1;
        for (int x = from.x; x != to.x + dir; x += dir) {
          path_.push_back({x, from.y});
        }
      }
    }

    path_mask_.assign(kBoardHeight, std::vector<bool>(kBoardWidth, false));
    for (const auto &p : path_) {
      for (int dy = -map.path_width + 1; dy <= map.path_width - 1; ++dy) {
        for (int dx = -map.path_width + 1; dx <= map.path_width - 1; ++dx) {
          const int cx = p.x + dx;
          const int cy = p.y + dy;
          if (cy >= 0 && cy < kBoardHeight && cx >= 0 && cx < kBoardWidth) {
            const auto yi = static_cast<size_t>(cy);
            const auto xi = static_cast<size_t>(cx);
            path_mask_[yi][xi] = true;
          }
        }
      }
    }
  }

  void StartWave() {
    if (wave_active_ || game_over_) {
      return;
    }
    ++wave_;
    spawn_remaining_ = 6 + DifficultyLevel() * 2;
    spawn_cooldown_ms_ = 0;
    wave_active_ = true;
    Sfx("wave_start");
  }

  void SpawnTick() {
    if (!wave_active_) {
      return;
    }

    if (spawn_remaining_ <= 0 && enemies_.empty()) {
      return;
    }

    spawn_cooldown_ms_ -= static_cast<int>(kTickMs * TimeScale());
    if (spawn_cooldown_ms_ > 0 || spawn_remaining_ <= 0) {
      return;
    }

    Enemy e;
    e.path_progress = 0.0F;
    const int diff = DifficultyLevel();
    e.type = SelectEnemyType(diff);
    ApplyEnemyStats(e, diff);
    const int width = std::max(1, CurrentMap().path_width);
    if (width > 1) {
      std::uniform_int_distribution<int> dist(-(width - 1), width - 1);
      e.lane_offset = dist(rng_);
    }
    enemies_.push_back(e);

    --spawn_remaining_;
    spawn_cooldown_ms_ = static_cast<int>(600.0F / kSpeedFactor);
  }

  void MoveEnemies() {
    int lives_before = lives_;
    for (auto &e : enemies_) {
      if (e.sleep_timer > 0.0F) {
        e.sleep_timer = std::max(0.0F, e.sleep_timer - Dt());
        continue;
      }
      e.path_progress += e.speed * Dt();
    }

    for (auto &e : enemies_) {
      const int end_index = static_cast<int>(path_.size() - 1);
      if (static_cast<int>(std::floor(e.path_progress)) >= end_index) {
        e.hp = 0;
        lives_ = std::max(0, lives_ - 1);
      }
    }
#ifdef ENABLE_AUDIO
    if (lives_ < lives_before) {
      Sfx("life_lost");
    }
#endif
  }

  std::optional<size_t> FindTargetAt(const Tower &t,
                                     const Vec2 &center) const {
    std::optional<size_t> best;
    float best_progress = -1.0F;
    const float range2 = t.range * t.range;

    for (size_t i = 0; i < enemies_.size(); ++i) {
      if (enemies_[i].hp <= 0) {
        continue;
      }
      const auto pos = EnemyCell(enemies_[i]);
      if (t.type != Tower::Type::Thunder) {
        const float d2 = DistanceSquared(center, pos);
        if (d2 > range2) {
          continue;
        }
      }
      if (enemies_[i].path_progress > best_progress) {
        best_progress = enemies_[i].path_progress;
        best = i;
      }
    }
    return best;
  }

  std::optional<size_t> FindTarget(const Tower &t) const {
    return FindTargetAt(t, TowerCenter(t));
  }

  void TowersAct() {
    for (auto &t : towers_) {
      t.cooldown -= Dt();
    }

    for (size_t i = 0; i < towers_.size(); ++i) {
      Tower &t = towers_[i];
      if (t.type == Tower::Type::Kitty || t.cooldown > 0.0F) {
        continue;
      }

      const auto target_index = FindTarget(t);
      if (!target_index.has_value()) {
        continue;
      }

      switch (t.type) {
      case Tower::Type::Default: {
        const auto c = TowerCenter(t);
        auto add_projectile = [&](const Enemy &target) {
          Projectile p;
          p.x = static_cast<float>(c.x);
          p.y = static_cast<float>(c.y);
          p.target = EnemyCell(target);
          p.speed = 17.0F;
          p.damage = t.damage;
          projectiles_.push_back(p);
        };
        if (t.upgraded) {
          std::vector<std::pair<float, size_t>> sorted;
          const float range2 = t.range * t.range;
          for (size_t i = 0; i < enemies_.size(); ++i) {
            if (enemies_[i].hp <= 0)
              continue;
            const auto pos = EnemyCell(enemies_[i]);
            if (DistanceSquared(c, pos) > range2)
              continue;
            sorted.push_back({enemies_[i].path_progress, i});
          }
          if (!sorted.empty()) {
            std::sort(sorted.begin(), sorted.end(),
                      [](auto &a, auto &b) { return a.first > b.first; });
            size_t front_idx = sorted.front().second;
            size_t back_idx = sorted.back().second;
            size_t mid_idx = sorted[sorted.size() / 2].second;
            add_projectile(enemies_[front_idx]);
            if (mid_idx != front_idx)
              add_projectile(enemies_[mid_idx]);
            if (back_idx != front_idx && back_idx != mid_idx)
              add_projectile(enemies_[back_idx]);
          }
        } else {
          auto &target = enemies_[*target_index];
          add_projectile(target);
        }
#ifdef ENABLE_AUDIO
        audio_->PlayEvent("tower_default_shoot");
#endif
        break;
      }
      case Tower::Type::Thunder: {
        FireLaser(t, enemies_[*target_index]);
#ifdef ENABLE_AUDIO
        audio_->PlayEvent("tower_thunder_shoot");
#endif
        break;
      }
      case Tower::Type::Fat: {
        FireShockwave(t);
#ifdef ENABLE_AUDIO
        audio_->PlayEvent("tower_fat_shoot");
#endif
        break;
      }
      case Tower::Type::Catatonic: {
        FireCatatonic(t);
#ifdef ENABLE_AUDIO
        audio_->PlayEvent("tower_catatonic_shoot");
#endif
        break;
      }
      case Tower::Type::Galactic: {
        FireGalactic(t, enemies_[*target_index]);
#ifdef ENABLE_AUDIO
        audio_->PlayEvent("tower_galactic_shoot");
#endif
        break;
      }
      case Tower::Type::Kitty:
        break; // handled separately
      }
      t.cooldown = NextCooldown(t.fire_rate);
    }

    HandleKittyAttacks();
  }

  void MoveProjectiles() {
    for (auto &p : projectiles_) {
      const float dx = static_cast<float>(p.target.x) - p.x;
      const float dy = static_cast<float>(p.target.y) - p.y;
      const float dist = std::sqrt(dx * dx + dy * dy);
      const float step = p.speed * Dt();
      if (dist <= step || dist < 1e-3F) {
        p.x = static_cast<float>(p.target.x);
        p.y = static_cast<float>(p.target.y);
        continue;
      }
      const float norm = step / dist;
      p.x += dx * norm;
      p.y += dy * norm;
    }
  }

  void ResolveProjectiles() {
    std::vector<Projectile> survivors;
    survivors.reserve(projectiles_.size());
    for (auto &p : projectiles_) {
      const float dx = static_cast<float>(p.target.x) - p.x;
      const float dy = static_cast<float>(p.target.y) - p.y;
      const float dist2 = dx * dx + dy * dy;
      if (dist2 > 0.05F) { // not arrived yet
        survivors.push_back(p);
        continue;
      }

      // Find nearest enemy to impact point.
      std::optional<size_t> hit_index;
      float best_d2 = 1.0F;
      for (size_t i = 0; i < enemies_.size(); ++i) {
        const auto pos = EnemyCell(enemies_[i]);
        const float ddx = static_cast<float>(pos.x) - p.x;
        const float ddy = static_cast<float>(pos.y) - p.y;
        const float d2 = ddx * ddx + ddy * ddy;
        if (d2 < best_d2) {
          best_d2 = d2;
          hit_index = i;
        }
      }

      if (hit_index.has_value()) {
        auto &target = enemies_[*hit_index];
        target.hp -= p.damage;
        if (target.hp <= 0) {
          kibbles_ += Bounty(target.type);
          PlayDeathSfx(target.type);
        } else {
          hit_splats_.push_back({EnemyCell(target), 0.28F});
        }
      }
    }
    projectiles_ = std::move(survivors);
  }

  void Cleanup() {
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [](const Enemy &e) { return e.hp <= 0; }),
                   enemies_.end());
  }

  EnemyType SelectEnemyType(int diff) {
    // Keep mice present throughout; taper their share as difficulty rises.
    const float mouse_share =
        std::clamp(0.40F - 0.015F * static_cast<float>(diff), 0.18F, 0.40F);
    const float roll = Rand(0.0F, 1.0F);
    if (roll < mouse_share) {
      return EnemyType::Mouse;
    }
    // Occasional big rats as mid bosses.
    if (diff >= 9 && Rand(0.0F, 1.0F) < 0.18F) {
      return EnemyType::BigRat;
    }
    // Chance for scary dogs once the player is a few maps in.
    if (map_index_ >= 3 && diff >= 16 && Rand(0.0F, 1.0F) < 0.08F) {
      return EnemyType::Dog;
    }
    return EnemyType::Rat;
  }

  void ApplyEnemyStats(Enemy &e, int diff) {
    switch (e.type) {
    case EnemyType::Mouse:
      e.max_hp = 2 + diff * 1;
      e.speed = (0.95F + diff * 0.05F) * kSpeedFactor;
      break;
    case EnemyType::Rat:
      e.max_hp = 5 + static_cast<int>(diff * 2.5F);
      e.speed = (0.65F + diff * 0.065F) * kSpeedFactor;
      break;
    case EnemyType::BigRat:
      e.max_hp = 15 + diff * 4;
      e.speed = (0.55F + diff * 0.045F) * kSpeedFactor;
      break;
    case EnemyType::Dog:
      e.max_hp = 28 + diff * 6;
      e.speed = (0.9F + diff * 0.055F) * kSpeedFactor;
      break;
    }
    e.hp = e.max_hp;
  }

  void UpdateHitSplats() {
    for (auto &hs : hit_splats_) {
      hs.time_left -= Dt();
    }
    hit_splats_.erase(
        std::remove_if(hit_splats_.begin(), hit_splats_.end(),
                       [](const HitSplat &hs) { return hs.time_left <= 0.0F; }),
        hit_splats_.end());
  }

  void CheckWaveCompletion() {
    if (!wave_active_) {
      return;
    }
    if (spawn_remaining_ > 0 || !enemies_.empty()) {
      return;
    }

    wave_active_ = false;
    kibbles_ += 20 + wave_ * 3;

    if (wave_ % 10 == 0) {
      AdvanceMap();
    }

    if (auto_waves_ && !game_over_) {
      StartWave();
    }
  }

  void AdvanceMap(bool dev_skip = false) {
    map_index_ = (map_index_ + 1) % static_cast<int>(maps_.size());
    wave_active_ = false;
    spawn_remaining_ = 0;
    enemies_.clear();
    towers_.clear();
    held_tower_.reset();
    // Preserve kibbles across maps to let players invest between stages.
    lives_ = kStartingLives;
    auto_waves_ = false;
    BuildPath();
    if (dev_skip) {
      wave_ = map_index_ * 10;
    }
#ifdef ENABLE_AUDIO
    audio_->SetMusicForMap(map_index_);
    audio_->PlayEvent("map_change");
#endif
  }

  void PlaceTower() {
    if (!overlay_enabled_) {
      return;
    }
    if (held_tower_.has_value()) {
      TryPlaceHeld();
      return;
    }

    const TowerDef def = GetDef(selected_type_);
    if (!IsUnlocked(def.type)) {
      return;
    }
    if (kibbles_ < def.cost) {
      return;
    }
    if (!CanPlace(cursor_, def.size, def.type, def.range, false)) {
      return;
    }

    Tower t;
    t.pos = cursor_;
    t.damage = def.damage;
    t.range = def.range;
    t.fire_rate = def.fire_rate;
    t.cooldown = Rand(0.05F, t.fire_rate); // offset starts for async cadence
    t.type = def.type;
    t.size = def.size;
    towers_.push_back(t);
    kibbles_ -= def.cost;
    Sfx("place");
  }

  Position EnemyCell(const Enemy &e) const {
    const int idx =
        static_cast<int>(std::clamp(std::floor(e.path_progress), 0.0F,
                                    static_cast<float>(path_.size() - 1)));
    const size_t i = static_cast<size_t>(idx);
    Position base = path_[i];
    int dx = 0;
    int dy = 0;
    if (i + 1 < path_.size()) {
      dx = path_[i + 1].x - base.x;
      dy = path_[i + 1].y - base.y;
    } else if (i > 0) {
      dx = base.x - path_[i - 1].x;
      dy = base.y - path_[i - 1].y;
    }
    dx = (dx > 0) - (dx < 0);
    dy = (dy > 0) - (dy < 0);
    Position perp{-dy, dx};
    base.x = std::clamp(base.x + perp.x * e.lane_offset, 0, kBoardWidth - 1);
    base.y = std::clamp(base.y + perp.y * e.lane_offset, 0, kBoardHeight - 1);
    return base;
  }

  ftxui::Color EnemyColor(const Enemy &e) const {
    const float ratio =
        static_cast<float>(e.hp) / static_cast<float>(std::max(1, e.max_hp));
    if (ratio > 0.75F) {
      return ftxui::Color::RedLight;
    }
    if (ratio > 0.5F) {
      return ftxui::Color::Orange1;
    }
    if (ratio > 0.25F) {
      return ftxui::Color::Yellow1;
    }
    return ftxui::Color::GrayLight;
  }

  bool InRange(const Vec2 &center, const Position &cell, float range) const {
    return DistanceSquared(center, cell) <= range * range;
  }

  float Rand(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
  }

  int Bounty(const EnemyType type) const {
    switch (type) {
    case EnemyType::Mouse:
      return 8;
    case EnemyType::Rat:
      return 12;
    case EnemyType::BigRat:
      return 20;
    case EnemyType::Dog:
      return 30;
    }
    return 12;
  }

  float TimeScale() const { return fast_forward_ ? kFastForwardMultiplier : 1.0F; }
  float Dt() const { return kTickSeconds * TimeScale(); }

  float NextCooldown(float base_rate) {
    const float scaled = base_rate / kSpeedFactor;
    return std::max(0.06F, scaled + Rand(-0.14F, 0.14F));
  }

  int DifficultyLevel() const {
    const int local = (wave_ - 1) % 10 + 1;
    const int map_bonus = map_index_ * 2;  // Softer ramp to allow longer runs.
    return local + map_bonus;
  }

  const MapDef &CurrentMap() const {
    return maps_[static_cast<size_t>(map_index_)];
  }

  void Sfx(const std::string &name) {
#ifdef ENABLE_AUDIO
    if (audio_)
      audio_->PlayEvent(name);
#endif
  }

  void PlayDeathSfx(EnemyType type) {
    switch (type) {
    case EnemyType::Mouse:
      Sfx("mouse_die");
      break;
    case EnemyType::Rat:
      Sfx("rat_die");
      break;
    case EnemyType::BigRat:
      Sfx("bigrat_die");
      break;
    case EnemyType::Dog:
      Sfx("dog_die");
      break;
    }
  }

  void SetMusic(int map_idx) {
#ifdef ENABLE_AUDIO
    if (audio_)
      audio_->SetMusicForMap(map_idx);
#endif
  }

  Vec2 TowerCenterAt(const Position &p, int size) const {
    const float cx =
        static_cast<float>(p.x) + (static_cast<float>(size) - 1.0F) / 2.0F;
    const float cy =
        static_cast<float>(p.y) + (static_cast<float>(size) - 1.0F) / 2.0F;
    return {cx, cy};
  }

  Vec2 TowerCenter(const Tower &t) const { return TowerCenterAt(t.pos, t.size); }

  std::string PadRight(const std::string &s, size_t w) const {
    if (s.size() >= w)
      return s;
    return s + std::string(w - s.size(), ' ');
  }

  int TypeKey(Tower::Type t) const {
    switch (t) {
    case Tower::Type::Default:
      return 1;
    case Tower::Type::Fat:
      return 2;
    case Tower::Type::Kitty:
      return 3;
    case Tower::Type::Thunder:
      return 4;
    case Tower::Type::Catatonic:
      return 5;
    case Tower::Type::Galactic:
      return 6;
    }
    return 0;
  }

  std::vector<TowerDef> SortedDefs() const {
    std::vector<TowerDef> defs = {GetDef(Tower::Type::Default),
                                  GetDef(Tower::Type::Fat),
                                  GetDef(Tower::Type::Kitty),
                                  GetDef(Tower::Type::Thunder),
                                  GetDef(Tower::Type::Catatonic),
                                  GetDef(Tower::Type::Galactic)};
    std::sort(defs.begin(), defs.end(),
              [](const TowerDef &a, const TowerDef &b) {
                if (a.cost == b.cost)
                  return a.name < b.name;
                return a.cost < b.cost;
              });
    return defs;
  }

  bool IsUnlocked(Tower::Type type) const {
    switch (type) {
    case Tower::Type::Default:
      return true;
    case Tower::Type::Fat:
      return unlocked_fat_;
    case Tower::Type::Kitty:
      return unlocked_kitty_;
    case Tower::Type::Thunder:
      return unlocked_thunder_;
    case Tower::Type::Catatonic:
      return unlocked_catatonic_;
    case Tower::Type::Galactic:
      return unlocked_galactic_;
    }
    return true;
  }

  void Unlock(Tower::Type type) {
    if (type == Tower::Type::Fat)
      unlocked_fat_ = true;
    if (type == Tower::Type::Kitty)
      unlocked_kitty_ = true;
    if (type == Tower::Type::Thunder)
      unlocked_thunder_ = true;
    if (type == Tower::Type::Catatonic)
      unlocked_catatonic_ = true;
    if (type == Tower::Type::Galactic)
      unlocked_galactic_ = true;
  }

  void TryUnlockOrSelect(Tower::Type type) {
    if (IsUnlocked(type)) {
      selected_type_ = type;
      return;
    }
    const auto def = GetDef(type);
    const int unlock_cost = def.cost * 10;
    if (kibbles_ >= unlock_cost) {
      kibbles_ -= unlock_cost;
      Unlock(type);
      selected_type_ = type;
      Sfx("unlock");
    }
  }

  bool OverlapsTower(const Position &p, int size) const {
    for (const auto &t : towers_) {
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

  bool OccupiesPath(const Position &p, int size) const {
    for (int dy = 0; dy < size; ++dy) {
      for (int dx = 0; dx < size; ++dx) {
        const int cx = p.x + dx;
        const int cy = p.y + dy;
        if (cx < 0 || cy < 0 || cx >= kBoardWidth || cy >= kBoardHeight) {
          return true;
        }
        if (path_mask_[static_cast<size_t>(cy)][static_cast<size_t>(cx)]) {
          return true;
        }
      }
    }
    return false;
  }

  bool CatatonicConflict(const Position &p, int size, Tower::Type type,
                         float range, bool upgraded) const {
    if (type != Tower::Type::Catatonic) {
      return false;
    }
    const float candidate_range = range + (upgraded ? 0.8F : 0.0F);
    const Vec2 center = {static_cast<float>(p.x) + (static_cast<float>(size) - 1.0F) / 2.0F,
                         static_cast<float>(p.y) + (static_cast<float>(size) - 1.0F) / 2.0F};
    for (const auto &t : towers_) {
      if (t.type != Tower::Type::Catatonic) {
        continue;
      }
      const Vec2 other = TowerCenter(t);
      const float dx = center.x - other.x;
      const float dy = center.y - other.y;
      const float dist2 = dx * dx + dy * dy;
      const float max_r = candidate_range + t.range + (t.upgraded ? 0.8F : 0.0F);
      if (dist2 <= max_r * max_r) {
        return true;
      }
    }
    return false;
  }

  bool CanPlace(const Position &p, int size, Tower::Type type,
                float range, bool upgraded) const {
    if (p.x < 0 || p.y < 0 || p.x + size - 1 >= kBoardWidth ||
        p.y + size - 1 >= kBoardHeight) {
      return false;
    }
    if (OccupiesPath(p, size)) {
      return false;
    }
    if (OverlapsTower(p, size)) {
      return false;
    }
    if (CatatonicConflict(p, size, type, range, upgraded)) {
      return false;
    }
    return true;
  }

  std::optional<size_t> TowerIndexAt(const Position &p) const {
    for (size_t i = 0; i < towers_.size(); ++i) {
      const auto &t = towers_[i];
      const int tx2 = t.pos.x + t.size - 1;
      const int ty2 = t.pos.y + t.size - 1;
      if (p.x >= t.pos.x && p.x <= tx2 && p.y >= t.pos.y && p.y <= ty2) {
        return i;
      }
    }
    return std::nullopt;
  }

  void PickUpTower() {
    if (held_tower_.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    HeldTower hold;
    hold.tower = towers_[*idx];
    hold.original = towers_[*idx].pos;
    held_tower_ = hold;
    towers_.erase(towers_.begin() + static_cast<long>(*idx));
    overlay_enabled_ = true; // ensure placement cues visible while holding
  }

  void TryPlaceHeld() {
    if (!held_tower_.has_value()) {
      return;
    }
    auto t = held_tower_->tower;
    if (!CanPlace(cursor_, t.size, t.type, t.range, t.upgraded)) {
      return;
    }
    t.pos = cursor_;
    t.cooldown = Rand(0.05F, t.fire_rate);
    towers_.push_back(t);
    held_tower_.reset();
    overlay_enabled_ = false;
  }

  void CancelHold() {
    if (!held_tower_.has_value()) {
      return;
    }
    auto t = held_tower_->tower;
    t.pos = held_tower_->original;
    towers_.push_back(t);
    held_tower_.reset();
    overlay_enabled_ = false;
  }

  void SellTowerAtCursor() {
    if (held_tower_.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    const Tower &t = towers_[*idx];
    const auto def = GetDef(t.type);
    const int refund = static_cast<int>(std::round(def.cost * 0.6F));
    kibbles_ += refund;
    towers_.erase(towers_.begin() + static_cast<long>(*idx));
    Sfx("sell");
  }

  void UpgradeTowerAtCursor() {
    if (held_tower_.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    Tower &t = towers_[*idx];
    if (t.upgraded) {
      return;
    }
    const auto def = GetDef(t.type);
    const int cost = def.cost * 5;
    if (kibbles_ < cost) {
      return;
    }
    kibbles_ -= cost;
    t.upgraded = true;
    if (t.type == Tower::Type::Fat) {
      t.range += 1.0F;
    } else if (t.type == Tower::Type::Thunder) {
      t.fire_rate = 0.2F;
      t.cooldown = 0.0F;
    }
    Sfx("unlock");
  }

  void FireLaser(const Tower &t, Enemy &target) {
    const auto center = TowerCenter(t);
    const auto target_cell = EnemyCell(target);
    const float dx = static_cast<float>(target_cell.x) - center.x;
    const float dy = static_cast<float>(target_cell.y) - center.y;
    const float len = std::max(0.001F, std::sqrt(dx * dx + dy * dy));
    const float ndx = dx / len;
    const float ndy = dy / len;

    // Apply damage to enemies near the line in front of the cat.
    for (auto &e : enemies_) {
      const auto pos = EnemyCell(e);
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
          kibbles_ += Bounty(e.type);
          PlayDeathSfx(e.type);
        } else {
          hit_splats_.push_back({EnemyCell(e), 0.18F});
        }
      }
    }

    // Build beam cells for rendering until board edge.
    Beam b;
    float bx = static_cast<float>(center.x);
    float by = static_cast<float>(center.y);
    for (int i = 0; i < 120; ++i) { // generous to cross the board
      const int cx = static_cast<int>(std::round(bx));
      const int cy = static_cast<int>(std::round(by));
      if (cx < 0 || cy < 0 || cx >= kBoardWidth || cy >= kBoardHeight) {
        break;
      }
      b.cells.push_back({cx, cy});
      bx += ndx * 0.5F;
      by += ndy * 0.5F;
    }
    beams_.push_back(std::move(b));
  }

  void FireShockwave(const Tower &t) {
    Shockwave sw;
    sw.center = TowerCenter(t);
    sw.radius = 0.0F;
    sw.max_radius = t.range;
    sw.speed = 10.0F;
    sw.time_left = 0.45F;
    shockwaves_.push_back(sw);

    for (auto &e : enemies_) {
      const auto pos = EnemyCell(e);
      if (InRange(sw.center, pos, t.range)) {
        e.hp -= t.damage;
        if (e.hp <= 0) {
          kibbles_ += Bounty(e.type);
          PlayDeathSfx(e.type);
        } else {
          hit_splats_.push_back({pos, 0.22F});
        }
      }
    }
  }

  void FireKitty(const Tower &t, Enemy &target) {
    const auto center = TowerCenter(t);
    const auto target_cell = EnemyCell(target);
    const auto area_cells = KittyAttackArea(center, target_cell);

    for (auto &e : enemies_) {
      const auto pos = EnemyCell(e);
      const bool hit = std::any_of(
          area_cells.begin(), area_cells.end(),
          [&](const Position &c) { return c.x == pos.x && c.y == pos.y; });
      if (!hit) {
        continue;
      }
      e.hp -= t.damage;
      if (e.hp <= 0) {
        kibbles_ += Bounty(e.type);
        PlayDeathSfx(e.type);
      } else {
        hit_splats_.push_back({pos, 0.18F});
      }
    }

    if (!area_cells.empty()) {
      area_highlights_.push_back({area_cells, 0.22F});
    }
  }

  void FireCatatonic(const Tower &t) {
    const float radius = t.upgraded ? t.range + 0.8F : t.range;
    const float sleep_dur =
        std::clamp(t.upgraded ? kCatSleepUpgrade : kCatSleepBase, 0.0F, kCatSleepCap);
    std::vector<Position> cells;
    for (auto &e : enemies_) {
      const auto pos = EnemyCell(e);
      if (InRange(TowerCenter(t), pos, radius)) {
        e.sleep_timer = std::min(kCatSleepCap, std::max(e.sleep_timer, sleep_dur));
        cells.push_back(pos);
      }
    }
    if (!cells.empty()) {
      area_highlights_.push_back(
          {cells, 0.6F, ftxui::Color::Purple, '~'});
    }
  }

  void FireGalactic(const Tower &t, Enemy &target) {
    const auto center = TowerCenter(t);
    const auto target_cell = EnemyCell(target);
    const float dx = static_cast<float>(target_cell.x) - center.x;
    const float dy = static_cast<float>(target_cell.y) - center.y;
    const float len = std::max(0.001F, std::sqrt(dx * dx + dy * dy));
    const float ndx = dx / len;
    const float ndy = dy / len;
    const float range = t.range;
    const float cone_cos = std::cos(0.6F); // ~60 deg cone

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
    for (auto &e : enemies_) {
      const auto pos = EnemyCell(e);
      const bool hit = std::any_of(
          cells.begin(), cells.end(),
          [&](const Position &c) { return c.x == pos.x && c.y == pos.y; });
      if (!hit)
        continue;
      const bool teleported = void_proc;
      if (teleported) {
        e.path_progress = std::max(0.0F, e.path_progress - kGalacticVoidBackstep);
      }
      e.hp -= t.damage;
      if (e.hp <= 0) {
        kibbles_ += Bounty(e.type);
        PlayDeathSfx(e.type);
      } else {
        hit_splats_.push_back({pos, 0.2F});
      }
    }

    if (!cells.empty()) {
      area_highlights_.push_back(
          {cells, void_proc ? 0.35F : 0.3F,
           void_proc ? ftxui::Color::DarkMagenta : ftxui::Color::LightSteelBlue,
           void_proc ? '~' : '*'});
    }
  }

  void UpdateShockwaves() {
    for (auto &sw : shockwaves_) {
      sw.radius += sw.speed * Dt();
      sw.time_left -= Dt();
    }
    shockwaves_.erase(std::remove_if(shockwaves_.begin(), shockwaves_.end(),
                                     [](const Shockwave &sw) {
                                       return sw.time_left <= 0.0F ||
                                              sw.radius > sw.max_radius;
                                     }),
                      shockwaves_.end());
  }

  void UpdateBeams() {
    for (auto &b : beams_) {
      b.time_left -= Dt();
    }
    beams_.erase(
        std::remove_if(beams_.begin(), beams_.end(),
                       [](const Beam &b) { return b.time_left <= 0.0F; }),
        beams_.end());
  }

  void UpdateAreas() {
    for (auto &a : area_highlights_) {
      a.time_left -= Dt();
    }
    area_highlights_.erase(std::remove_if(area_highlights_.begin(),
                                          area_highlights_.end(),
                                          [](const AreaHighlight &a) {
                                            return a.time_left <= 0.0F;
                                          }),
                           area_highlights_.end());
  }

  void BuildMaps() {
    maps_.clear();
    maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                            {12, kBoardHeight / 2},
                            {12, 4},
                            {30, 4},
                            {30, kBoardHeight - 5},
                            {kBoardWidth - 1, kBoardHeight - 5}},
                           1,
                           ftxui::Color::DarkGreen,
                           ftxui::Color::DarkGoldenrod});
    maps_.push_back(MapDef{{{0, 3},
                            {10, 3},
                            {10, 12},
                            {25, 12},
                            {25, kBoardHeight - 6},
                            {kBoardWidth - 1, kBoardHeight - 6}},
                           2,
                           ftxui::Color::DarkSlateGray3,
                           ftxui::Color::DarkTurquoise});
    maps_.push_back(MapDef{{{0, kBoardHeight - 4},
                            {15, kBoardHeight - 4},
                            {15, 6},
                            {32, 6},
                            {32, kBoardHeight / 2},
                            {kBoardWidth - 1, kBoardHeight / 2}},
                           1,
                           ftxui::Color::DarkOliveGreen3,
                           ftxui::Color::Gold3});
    maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                            {8, kBoardHeight / 2},
                            {8, 6},
                            {20, 6},
                            {20, kBoardHeight - 8},
                            {35, kBoardHeight - 8},
                            {35, 5},
                            {kBoardWidth - 1, 5}},
                           3,
                           ftxui::Color::DarkBlue,
                           ftxui::Color::CornflowerBlue});
    maps_.push_back(MapDef{{{0, 8},
                            {14, 8},
                            {14, kBoardHeight - 6},
                            {28, kBoardHeight - 6},
                            {28, 6},
                            {kBoardWidth - 1, 6}},
                           2,
                           ftxui::Color::DarkKhaki,
                           ftxui::Color::DarkOrange});
    maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                            {10, kBoardHeight / 2},
                            {10, 3},
                            {20, 3},
                            {20, kBoardHeight - 4},
                            {40, kBoardHeight - 4},
                            {40, 8},
                            {kBoardWidth - 1, 8}},
                           2,
                           ftxui::Color::DarkSlateGray1,
                           ftxui::Color::LightSkyBlue1});
    maps_.push_back(MapDef{{{0, kBoardHeight - 5},
                            {18, kBoardHeight - 5},
                            {18, 5},
                            {kBoardWidth - 2, 5},
                            {kBoardWidth - 2, kBoardHeight / 2}},
                           1,
                           ftxui::Color::DarkOliveGreen3,
                           ftxui::Color::GreenYellow});
    maps_.push_back(MapDef{{{0, 4},
                            {8, 4},
                            {8, kBoardHeight - 4},
                            {24, kBoardHeight - 4},
                            {24, 4},
                            {kBoardWidth - 1, 4}},
                           2,
                           ftxui::Color::DarkMagenta,
                           ftxui::Color::DeepPink3});
    maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                            {12, kBoardHeight / 2},
                            {12, 6},
                            {22, 6},
                            {22, kBoardHeight - 7},
                            {34, kBoardHeight - 7},
                            {34, 5},
                            {kBoardWidth - 1, 5}},
                           2,
                           ftxui::Color::DarkSeaGreen3,
                           ftxui::Color::Chartreuse1});
    maps_.push_back(MapDef{{{0, 2},
                            {16, 2},
                            {16, kBoardHeight - 3},
                            {30, kBoardHeight - 3},
                            {30, 7},
                            {kBoardWidth - 1, 7}},
                           3,
                           ftxui::Color::DarkRed,
                           ftxui::Color::OrangeRed1});
  }

  ftxui::Element RenderBoard() const {
    std::vector<std::vector<char>> glyphs(kBoardHeight,
                                          std::vector<char>(kBoardWidth, ' '));
    const auto &map = CurrentMap();
    std::vector<std::vector<ftxui::Color>> backgrounds(
        kBoardHeight, std::vector<ftxui::Color>(kBoardWidth, map.background));
    std::vector<std::vector<ftxui::Color>> foregrounds(
        kBoardHeight,
        std::vector<ftxui::Color>(kBoardWidth, ftxui::Color::White));
    std::vector<std::vector<bool>> highlight(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    std::vector<std::vector<bool>> enemy_mask(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));

    const bool show_overlay = overlay_enabled_ || held_tower_.has_value();
    std::vector<std::vector<bool>> range_hint_base(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    std::vector<std::vector<bool>> range_hint_preview(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    if (show_overlay) {
      for (const auto &t : towers_) {
        const auto def = GetDef(t.type);
        if (!def.show_range) {
          continue;
        }
        const auto center = TowerCenter(t);
        for (int y = 0; y < kBoardHeight; ++y) {
          for (int x = 0; x < kBoardWidth; ++x) {
            Position cell{x, y};
            if (InRange(center, cell, t.range)) {
              range_hint_base[static_cast<size_t>(y)][static_cast<size_t>(x)] =
                  true;
            }
          }
        }
      }
      const TowerDef preview_def = GetDef(selected_type_);
      if (preview_def.show_range) {
        const Vec2 center{
            static_cast<float>(cursor_.x) +
                (static_cast<float>(preview_def.size) - 1.0F) / 2.0F,
            static_cast<float>(cursor_.y) +
                (static_cast<float>(preview_def.size) - 1.0F) / 2.0F};
        for (int y = 0; y < kBoardHeight; ++y) {
          for (int x = 0; x < kBoardWidth; ++x) {
            Position cell{x, y};
            if (InRange(center, cell, preview_def.range)) {
              range_hint_preview[static_cast<size_t>(y)]
                                [static_cast<size_t>(x)] = true;
            }
          }
        }
      }
    }

    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        if (path_mask_[yi][xi]) {
          backgrounds[yi][xi] = map.path_color;
          glyphs[yi][xi] = '.';
          foregrounds[yi][xi] = ftxui::Color::Black;
        }
      }
    }

    for (const auto &t : towers_) {
      const char glyph =
          t.type == Tower::Type::Thunder    ? (t.upgraded ? 'T' : 't')
          : t.type == Tower::Type::Fat      ? (t.upgraded ? 'F' : 'f')
          : t.type == Tower::Type::Kitty    ? (t.upgraded ? 'K' : 'k')
          : t.type == Tower::Type::Catatonic ? (t.upgraded ? 'C' : 'c')
          : t.type == Tower::Type::Galactic ? (t.upgraded ? 'G' : 'g')
                                            : (t.upgraded ? 'D' : 'd');
      const ftxui::Color bg =
          t.type == Tower::Type::Thunder     ? ftxui::Color::Blue1
          : t.type == Tower::Type::Fat       ? ftxui::Color::DarkOliveGreen3
          : t.type == Tower::Type::Kitty     ? ftxui::Color::Pink1
          : t.type == Tower::Type::Catatonic ? ftxui::Color::Purple
          : t.type == Tower::Type::Galactic  ? ftxui::Color::LightSteelBlue
                                             : ftxui::Color::Gold1;
      for (int dy = 0; dy < t.size; ++dy) {
        for (int dx = 0; dx < t.size; ++dx) {
          const int gx = t.pos.x + dx;
          const int gy = t.pos.y + dy;
          if (gx < 0 || gy < 0 || gx >= kBoardWidth || gy >= kBoardHeight) {
            continue;
          }
          const auto yi = static_cast<size_t>(gy);
          const auto xi = static_cast<size_t>(gx);
          glyphs[yi][xi] = glyph;
          backgrounds[yi][xi] = bg;
          foregrounds[yi][xi] = ftxui::Color::Black;
          highlight[yi][xi] = true;
        }
      }
    }

    for (const auto &e : enemies_) {
      const auto pos = EnemyCell(e);
      const auto yi = static_cast<size_t>(pos.y);
      const auto xi = static_cast<size_t>(pos.x);
      char g = 'r';
      ftxui::Color fg = EnemyColor(e);
      std::optional<ftxui::Color> bg_override;
      switch (e.type) {
      case EnemyType::Mouse:
        g = 'm';
        fg = ftxui::Color::Grey70;
        bg_override = std::nullopt; // keep path background
        break;
      case EnemyType::Rat:
        g = 'r';
        fg = EnemyColor(e);
        bg_override = ftxui::Color::Grey23;
        break;
      case EnemyType::BigRat:
        g = 'R';
        fg = ftxui::Color::RedLight;
        bg_override = ftxui::Color::Grey35;
        break;
      case EnemyType::Dog:
        g = 'D';
        fg = ftxui::Color::White;
        bg_override = ftxui::Color::DarkRed;
        break;
      }
      glyphs[yi][xi] = g;
      if (bg_override.has_value())
        backgrounds[yi][xi] = *bg_override;
      foregrounds[yi][xi] = fg;
      enemy_mask[yi][xi] = true;
    }

    for (const auto &p : projectiles_) {
      const int px = static_cast<int>(std::round(p.x));
      const int py = static_cast<int>(std::round(p.y));
      if (py < 0 || py >= kBoardHeight || px < 0 || px >= kBoardWidth) {
        continue;
      }
      const auto yi = static_cast<size_t>(py);
      const auto xi = static_cast<size_t>(px);
      glyphs[yi][xi] = '*';
      foregrounds[yi][xi] = ftxui::Color::SkyBlue1;
    }

    for (const auto &b : beams_) {
      for (const auto &cell : b.cells) {
        if (cell.y < 0 || cell.y >= kBoardHeight || cell.x < 0 ||
            cell.x >= kBoardWidth) {
          continue;
        }
        const auto yi = static_cast<size_t>(cell.y);
        const auto xi = static_cast<size_t>(cell.x);
        glyphs[yi][xi] = '-';
        foregrounds[yi][xi] = ftxui::Color::CyanLight;
      }
    }

    for (const auto &ah : area_highlights_) {
      for (const auto &cell : ah.cells) {
        if (cell.y < 0 || cell.y >= kBoardHeight || cell.x < 0 ||
            cell.x >= kBoardWidth) {
          continue;
        }
        const auto yi = static_cast<size_t>(cell.y);
        const auto xi = static_cast<size_t>(cell.x);
        glyphs[yi][xi] = ah.glyph;
        foregrounds[yi][xi] = ah.color;
        backgrounds[yi][xi] = BlendColor(backgrounds[yi][xi], ah.color, 0.08F);
      }
    }

    for (const auto &sw : shockwaves_) {
      for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
          Position cell{x, y};
          const float dist = std::sqrt(DistanceSquared(sw.center, cell));
          if (std::abs(dist - sw.radius) < 0.6F) {
            const auto yi = static_cast<size_t>(y);
            const auto xi = static_cast<size_t>(x);
            glyphs[yi][xi] = 'o';
            foregrounds[yi][xi] = ftxui::Color::YellowLight;
          }
        }
      }
    }

    for (const auto &hs : hit_splats_) {
      if (hs.pos.y < 0 || hs.pos.y >= kBoardHeight || hs.pos.x < 0 ||
          hs.pos.x >= kBoardWidth) {
        continue;
      }
      const auto yi = static_cast<size_t>(hs.pos.y);
      const auto xi = static_cast<size_t>(hs.pos.x);
      glyphs[yi][xi] = 'x';
      backgrounds[yi][xi] = ftxui::Color::White;
      foregrounds[yi][xi] = ftxui::Color::Red3;
    }

    const auto apply_tint = [&](size_t yi, size_t xi, const ftxui::Color &tint,
                                float alpha) {
      backgrounds[yi][xi] = BlendColor(backgrounds[yi][xi], tint, alpha);
    };

    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        if (!enemy_mask[yi][xi]) {
          if (range_hint_base[yi][xi]) {
            apply_tint(yi, xi, ftxui::Color::DarkSeaGreen, 0.25F);
          }
          if (range_hint_preview[yi][xi]) {
            apply_tint(yi, xi, ftxui::Color::LightSkyBlue1, 0.45F);
          }
        }
      }
    }

    if (show_overlay) {
      const TowerDef preview_def_place = GetDef(
          held_tower_.has_value() ? held_tower_->tower.type : selected_type_);
      const bool can_place_preview =
          cursor_.x >= 0 && cursor_.y >= 0 &&
          cursor_.x + preview_def_place.size - 1 < kBoardWidth &&
          cursor_.y + preview_def_place.size - 1 < kBoardHeight &&
          !OccupiesPath(cursor_, preview_def_place.size) &&
          !OverlapsTower(cursor_, preview_def_place.size);
      for (int dy = 0; dy < preview_def_place.size; ++dy) {
        for (int dx = 0; dx < preview_def_place.size; ++dx) {
          const int gx = cursor_.x + dx;
          const int gy = cursor_.y + dy;
          if (gx < 0 || gy < 0 || gx >= kBoardWidth || gy >= kBoardHeight) {
            continue;
          }
          const auto yi = static_cast<size_t>(gy);
          const auto xi = static_cast<size_t>(gx);
          glyphs[yi][xi] = can_place_preview ? '+' : 'X';
          foregrounds[yi][xi] = can_place_preview
                                    ? ftxui::Color(ftxui::Color::LightSkyBlue1)
                                    : ftxui::Color(ftxui::Color::RedLight);
        }
      }
    }

    std::vector<ftxui::Element> rows;
    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      std::vector<ftxui::Element> cells;
      cells.reserve(kBoardWidth);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        auto cell = text(std::string(1, glyphs[yi][xi]) + " ") |
                    bgcolor(backgrounds[yi][xi]) | color(foregrounds[yi][xi]);
        if (highlight[yi][xi]) {
          cell = cell | bold;
        }
        if (cursor_.x == x && cursor_.y == y) {
          cell = cell | inverted;
        }
        cells.push_back(std::move(cell));
      }
      rows.push_back(hbox(std::move(cells)));
    }

    auto board_elem = vbox(std::move(rows));
    if (game_over_) {
      board_elem = board_elem | bgcolor(ftxui::Color::Black) |
                   color(ftxui::Color::RedLight) | bold;
    }
    return board_elem;
  }

  ftxui::Element RenderStats() const {
    std::string wave_text =
        wave_active_ ? "Wave " + std::to_string(wave_) : "Waiting";
    if (auto_waves_) {
      wave_text += " (auto)";
    }
    std::vector<ftxui::Element> lines;
    lines.push_back(text("cat cat"));
    if (dev_mode_) {
      lines.push_back(text("DEV MODE"));
    }
    lines.push_back(text("Status: " + wave_text));
    lines.push_back(text("Map: " + std::to_string(map_index_ + 1) + "/10"));
    lines.push_back(text(
        "Speed: " + std::string(fast_forward_ ? "FAST x5 (f)" : "Normal (f)")));
    lines.push_back(text("Lives: " + std::to_string(lives_)));
    lines.push_back(text("Kibbles: " + std::to_string(kibbles_)));
    lines.push_back(text("Cats: " + std::to_string(towers_.size())));
    lines.push_back(separator());

    const auto selected_def = GetDef(selected_type_);
    lines.push_back(text("Selected: " + selected_def.name));
    lines.push_back(separator());

    const auto defs = SortedDefs();

    if (view_shop_) {
      lines.push_back(text("shop (press 1-6 to buy/select, p to return)"));
      std::vector<TowerDef> locked;
      for (const auto &d : defs) {
        if (!IsUnlocked(d.type)) {
          locked.push_back(d);
        }
      }
      if (locked.empty()) {
        lines.push_back(text("All cats unlocked!"));
      } else {
        size_t name_w = 0;
        size_t cost_w = 0;
        std::vector<std::string> cost_cols(locked.size());
        for (size_t i = 0; i < locked.size(); ++i) {
          const auto &d = locked[i];
          const int unlock_cost = d.cost * 10;
          name_w = std::max(name_w, d.name.size() + 3); // account for key
          cost_cols[i] = "unlock " + std::to_string(unlock_cost);
          cost_w = std::max(cost_w, cost_cols[i].size());
        }
        for (size_t i = 0; i < locked.size(); ++i) {
          const auto &d = locked[i];
          std::string desc;
          if (d.type == Tower::Type::Thunder) {
            desc = "Laser, dmg " + std::to_string(d.damage) + ", slow fire";
          } else if (d.type == Tower::Type::Fat) {
            desc = "2x2 AOE, dmg " + std::to_string(d.damage);
          } else if (d.type == Tower::Type::Kitty) {
            desc = "Swipe 4x6, dmg " + std::to_string(d.damage) +
                   " (jumps when upgraded)";
          } else if (d.type == Tower::Type::Catatonic) {
            desc = "Sleep pulse, slows";
          } else if (d.type == Tower::Type::Galactic) {
            desc = "Cosmic cone blast";
          } else {
            desc = "dmg " + std::to_string(d.damage);
          }
          const std::string key = std::to_string(TypeKey(d.type)) + ") ";
          const std::string line = PadRight(key + d.name, name_w + 2) +
                                   PadRight(cost_cols[i], cost_w + 2) + desc;
          lines.push_back(text(line));
        }
      }
    } else {
      lines.push_back(text("press p to view shop"));
      lines.push_back(text("unlocked cats:"));
      for (const auto &d : defs) {
        if (!IsUnlocked(d.type)) {
          continue;
        }
        std::string line = std::to_string(TypeKey(d.type)) + ") " + d.name +
                           " (" + std::to_string(d.cost) + " kib)";
        lines.push_back(text(line));
      }
    }

    if (game_over_) {
      lines.push_back(text("Game Over") | bold | color(ftxui::Color::RedLight));
    }

    if (show_controls_) {
      lines.push_back(separator());
      lines.push_back(text("controls (press h to hide):"));
      lines.push_back(text("arrows/WASD - move cursor"));
      lines.push_back(text("space/c     - place selected cat"));
      lines.push_back(text("m           - pick up tower under cursor"));
      lines.push_back(text("u           - upgrade tower (cost 5x)"));
      lines.push_back(text("x           - sell tower (60% refund)"));
      lines.push_back(text("esc         - toggle overlay / cancel move"));
      lines.push_back(text("1-6         - select cat type (by cost)"));
      lines.push_back(text("p           - toggle shop view"));
      lines.push_back(text("n/N         - next wave / auto waves"));
      lines.push_back(text("f           - toggle fast forward x5"));
      lines.push_back(text("t           - toggle sfx"));
      lines.push_back(text("y           - toggle music"));
      if (dev_mode_) {
        lines.push_back(text(">           - skip to next map (dev)"));
      }
      lines.push_back(text("q           - quit"));
    } else {
      lines.push_back(separator());
      lines.push_back(text("press h for controls"));
    }

    lines.push_back(separator());
    lines.push_back(text("Goal: Keep rats from reaching the burrow!"));

    return vbox(std::move(lines));
  }

  std::vector<Position> path_;
  std::vector<std::vector<bool>> path_mask_;
  std::vector<Enemy> enemies_;
  std::vector<Tower> towers_;
  std::vector<HitSplat> hit_splats_;
  std::vector<Projectile> projectiles_;
  std::vector<Shockwave> shockwaves_;
  std::vector<Beam> beams_;
  std::vector<AreaHighlight> area_highlights_;
  std::optional<HeldTower> held_tower_;
  std::vector<MapDef> maps_;
  Position cursor_{};

  std::mt19937 rng_{std::random_device{}()};
  std::unique_ptr<AudioSystem> audio_;

  Tower::Type selected_type_ = Tower::Type::Default;
  bool unlocked_thunder_ = false;
  bool unlocked_fat_ = false;
  bool unlocked_kitty_ = false;
  bool unlocked_catatonic_ = false;
  bool unlocked_galactic_ = false;
  bool view_shop_ = false;
  bool overlay_enabled_ = true;
  bool show_controls_ = false;
  bool auto_waves_ = false;
  bool fast_forward_ = false;
  bool dev_mode_ = false;
  int map_index_ = 0;
  int kibbles_ = 0;
  int lives_ = 0;
  int wave_ = 0;
  bool wave_active_ = false;
  bool game_over_ = false;
  int spawn_remaining_ = 0;
  int spawn_cooldown_ms_ = 0;
};

class GameComponent : public ftxui::ComponentBase {
public:
  GameComponent(ftxui::ScreenInteractive &screen, bool dev_mode)
      : game_(dev_mode), screen_(screen) {
    ticker_ = std::thread([this] {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
        screen_.Post(ftxui::Event::Custom);
      }
    });
  }

  ~GameComponent() override {
    running_ = false;
    if (ticker_.joinable()) {
      ticker_.join();
    }
  }

  ftxui::Element Render() override { return game_.Render(); }

  bool OnEvent(ftxui::Event event) override {
    if (event == ftxui::Event::Character('q')) {
      running_ = false;
      screen_.Exit();
      return true;
    }

    if (event == ftxui::Event::Custom) {
      game_.Tick();
      if (game_.GameOver()) {
        running_ = false;
      }
      return true;
    }

    return game_.HandleEvent(event);
  }

private:
  Game game_;
  ftxui::ScreenInteractive &screen_;
  std::atomic<bool> running_{true};
  std::thread ticker_;
};

} // namespace

int main(int argc, const char* argv[]) {
  bool dev_mode = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--dev") {
      dev_mode = true;
    }
  }
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto component = ftxui::Make<GameComponent>(screen, dev_mode);
  screen.Loop(component);
  return 0;
}
