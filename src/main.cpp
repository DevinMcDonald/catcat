#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

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

constexpr int kBoardWidth = 24;
constexpr int kBoardHeight = 14;
constexpr int kTickMs = 16;  // ~60 FPS
constexpr float kTickSeconds = kTickMs / 1000.0F;
constexpr int kStartingCoins = 90;
constexpr int kStartingLives = 15;

struct Position {
  int x = 0;
  int y = 0;
};

float DistanceSquared(const Position& a, const Position& b) {
  const float dx = static_cast<float>(a.x - b.x);
  const float dy = static_cast<float>(a.y - b.y);
  return dx * dx + dy * dy;
}

ftxui::Color BlendColor(const ftxui::Color& base, const ftxui::Color& overlay,
                        float alpha) {
  return ftxui::Color::Interpolate(alpha, base, overlay);
}

struct Enemy {
  float path_progress = 0.0F;  // index along path cells
  float speed = 1.0F;          // cells per second
  int hp = 1;
  int max_hp = 1;
};

struct Tower {
  enum class Type { Default, Thunder, Fat };

  Position pos{};
  int damage = 2;
  float range = 3.2F;
  float cooldown = 0.0F;       // time until next shot
  float fire_rate = 1.2F;      // seconds between shots
  Type type = Type::Default;
  int size = 1;  // 1x1 or 2x2 for Fat
};

struct HitSplat {
  Position pos{};
  float time_left = 0.25F;  // seconds
};

struct Projectile {
  float x = 0.0F;
  float y = 0.0F;
  Position target{};
  float speed = 17.0F;  // cells per second
  int damage = 0;
};

struct Shockwave {
  Position center{};
  float radius = 0.0F;
  float max_radius = 0.0F;
  float speed = 10.0F;
  float time_left = 0.4F;
};

struct Beam {
  std::vector<Position> cells;
  float time_left = 0.18F;
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

TowerDef GetDef(Tower::Type type) {
  switch (type) {
    case Tower::Type::Default:
      return {type, "Default Cat", 35, 3, 3.5F, 0.85F, true, 1};
    case Tower::Type::Thunder:
      return {type, "Thundercat", 75, 6, 999.0F, 1.35F, false, 1};
    case Tower::Type::Fat:
      return {type, "Fat Cat", 55, 4, 3.0F, 1.4F, true, 2};
  }
  return {Tower::Type::Default, "Default Cat", 35, 3, 3.5F, 0.85F, true, 1};
}

class Game {
 public:
  Game() {
    BuildPath();
    coins_ = kStartingCoins;
    lives_ = kStartingLives;
    cursor_ = {3, kBoardHeight / 2};
  }

  void Tick() {
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
    Cleanup();
    UpdateHitSplats();
    CheckWaveCompletion();
    if (lives_ <= 0) {
      game_over_ = true;
    }
  }

  bool HandleEvent(const ftxui::Event& event) {
    if (game_over_) {
      return false;
    }

    const auto move_cursor = [&](int dx, int dy) {
      cursor_.x = std::clamp(cursor_.x + dx, 0, kBoardWidth - 1);
      cursor_.y = std::clamp(cursor_.y + dy, 0, kBoardHeight - 1);
    };

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('w')) {
      move_cursor(0, -1);
      return true;
    }
    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('s')) {
      move_cursor(0, 1);
      return true;
    }
    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('a')) {
      move_cursor(-1, 0);
      return true;
    }
    if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('d')) {
      move_cursor(1, 0);
      return true;
    }

    if (event == ftxui::Event::Character('c') || event == ftxui::Event::Character(' ')) {
      PlaceTower();
      return true;
    }

    if (event == ftxui::Event::Character('n')) {
      StartWave();
      return true;
    }

    if (event == ftxui::Event::Character('1')) {
      selected_type_ = Tower::Type::Default;
      return true;
    }
    if (event == ftxui::Event::Character('2')) {
      selected_type_ = Tower::Type::Thunder;
      return true;
    }
    if (event == ftxui::Event::Character('3')) {
      selected_type_ = Tower::Type::Fat;
      return true;
    }

    return false;
  }

  ftxui::Element Render() const {
    auto board = RenderBoard();
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
    // Create a winding path that snakes through the board.
    const std::vector<Position> anchors = {
        {0, kBoardHeight / 2},
        {5, kBoardHeight / 2},
        {5, 2},
        {15, 2},
        {15, kBoardHeight - 3},
        {kBoardWidth - 1, kBoardHeight - 3},
    };

    for (size_t i = 1; i < anchors.size(); ++i) {
      const auto& from = anchors[i - 1];
      const auto& to = anchors[i];
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
    for (const auto& p : path_) {
      if (p.y >= 0 && p.y < kBoardHeight && p.x >= 0 && p.x < kBoardWidth) {
        const auto yi = static_cast<size_t>(p.y);
        const auto xi = static_cast<size_t>(p.x);
        path_mask_[yi][xi] = true;
      }
    }
  }

  void StartWave() {
    if (wave_active_ || game_over_) {
      return;
    }
    ++wave_;
    spawn_remaining_ = 6 + wave_ * 2;
    spawn_cooldown_ms_ = 0;
    wave_active_ = true;
  }

  void SpawnTick() {
    if (!wave_active_) {
      return;
    }

    if (spawn_remaining_ <= 0 && enemies_.empty()) {
      return;
    }

    spawn_cooldown_ms_ -= kTickMs;
    if (spawn_cooldown_ms_ > 0 || spawn_remaining_ <= 0) {
      return;
    }

    Enemy e;
    e.path_progress = 0.0F;
    e.speed = 0.65F + static_cast<float>(wave_) * 0.07F;
    e.max_hp = 6 + wave_ * 3;
    e.hp = e.max_hp;
    enemies_.push_back(e);

    --spawn_remaining_;
    spawn_cooldown_ms_ = 600;
  }

  void MoveEnemies() {
    for (auto& e : enemies_) {
      e.path_progress += e.speed * kTickSeconds;
    }

    for (auto& e : enemies_) {
      const int end_index = static_cast<int>(path_.size() - 1);
      if (static_cast<int>(std::floor(e.path_progress)) >= end_index) {
        e.hp = 0;
        lives_ = std::max(0, lives_ - 1);
      }
    }
  }

  std::optional<size_t> FindTarget(const Tower& t) const {
    std::optional<size_t> best;
    float best_progress = -1.0F;
    const float range2 = t.range * t.range;
    const Position center = TowerCenter(t);

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

  void TowersAct() {
    for (auto& t : towers_) {
      t.cooldown -= kTickSeconds;
      if (t.cooldown > 0.0F) {
        continue;
      }

      const auto target_index = FindTarget(t);
      if (!target_index.has_value()) {
        continue;
      }

      switch (t.type) {
        case Tower::Type::Default: {
          auto& target = enemies_[*target_index];
          Projectile p;
          const auto c = TowerCenter(t);
          p.x = static_cast<float>(c.x);
          p.y = static_cast<float>(c.y);
          p.target = EnemyCell(target);
          p.speed = 17.0F;
          p.damage = t.damage;
          projectiles_.push_back(p);
          break;
        }
        case Tower::Type::Thunder: {
          FireLaser(t, enemies_[*target_index]);
          break;
        }
        case Tower::Type::Fat: {
          FireShockwave(t);
          break;
        }
      }
      t.cooldown = NextCooldown(t.fire_rate);
    }
  }

  void MoveProjectiles() {
    for (auto& p : projectiles_) {
      const float dx = static_cast<float>(p.target.x) - p.x;
      const float dy = static_cast<float>(p.target.y) - p.y;
      const float dist = std::sqrt(dx * dx + dy * dy);
      const float step = p.speed * kTickSeconds;
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
    for (auto& p : projectiles_) {
      const float dx = static_cast<float>(p.target.x) - p.x;
      const float dy = static_cast<float>(p.target.y) - p.y;
      const float dist2 = dx * dx + dy * dy;
      if (dist2 > 0.05F) {  // not arrived yet
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
        auto& target = enemies_[*hit_index];
        target.hp -= p.damage;
        if (target.hp <= 0) {
          coins_ += 12;
        } else {
          hit_splats_.push_back({EnemyCell(target), 0.28F});
        }
      }
    }
    projectiles_ = std::move(survivors);
  }

  void Cleanup() {
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [](const Enemy& e) { return e.hp <= 0; }),
                   enemies_.end());
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
                                      [](const Projectile& p) {
                                        return std::isnan(p.x);
                                      }),
                       projectiles_.end());
  }

  void UpdateHitSplats() {
    for (auto& hs : hit_splats_) {
      hs.time_left -= kTickSeconds;
    }
    hit_splats_.erase(std::remove_if(hit_splats_.begin(), hit_splats_.end(),
                                     [](const HitSplat& hs) {
                                       return hs.time_left <= 0.0F;
                                     }),
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
    coins_ += 20 + wave_ * 3;
  }

  void PlaceTower() {
    const TowerDef def = GetDef(selected_type_);
    if (coins_ < def.cost) {
      return;
    }
    if (cursor_.x < 0 || cursor_.x >= kBoardWidth || cursor_.y < 0 ||
        cursor_.y >= kBoardHeight) {
      return;
    }
    if (cursor_.x + def.size - 1 >= kBoardWidth ||
        cursor_.y + def.size - 1 >= kBoardHeight) {
      return;
    }
    if (OccupiesPath(cursor_, def.size)) {
      return;
    }
    if (OverlapsTower(cursor_, def.size)) {
      return;
    }

    Tower t;
    t.pos = cursor_;
    t.damage = def.damage;
    t.range = def.range;
    t.fire_rate = def.fire_rate;
    t.cooldown = Rand(0.05F, t.fire_rate);  // offset starts for async cadence
    t.type = def.type;
    t.size = def.size;
    towers_.push_back(t);
    coins_ -= def.cost;
  }

  Position EnemyCell(const Enemy& e) const {
    const int idx = static_cast<int>(
        std::clamp(std::floor(e.path_progress), 0.0F,
                   static_cast<float>(path_.size() - 1)));
    return path_[static_cast<size_t>(idx)];
  }

  ftxui::Color EnemyColor(const Enemy& e) const {
    const float ratio = static_cast<float>(e.hp) /
                        static_cast<float>(std::max(1, e.max_hp));
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

  bool InRange(const Position& center, const Position& cell, float range) const {
    return DistanceSquared(center, cell) <= range * range;
  }

  float Rand(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
  }

  float NextCooldown(float base_rate) { return std::max(0.08F, base_rate + Rand(-0.18F, 0.18F)); }

  Position TowerCenter(const Tower& t) const {
    return {t.pos.x + t.size / 2, t.pos.y + t.size / 2};
  }

  bool OverlapsTower(const Position& p, int size) const {
    for (const auto& t : towers_) {
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

  bool OccupiesPath(const Position& p, int size) const {
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

  void FireLaser(const Tower& t, Enemy& target) {
    const auto center = TowerCenter(t);
    const auto target_cell = EnemyCell(target);
    const float dx = static_cast<float>(target_cell.x - center.x);
    const float dy = static_cast<float>(target_cell.y - center.y);
    const float len = std::max(0.001F, std::sqrt(dx * dx + dy * dy));
    const float ndx = dx / len;
    const float ndy = dy / len;

    // Apply damage to enemies near the line in front of the cat.
    for (auto& e : enemies_) {
      const auto pos = EnemyCell(e);
      const float vx = static_cast<float>(pos.x - center.x);
      const float vy = static_cast<float>(pos.y - center.y);
      const float dot = vx * ndx + vy * ndy;
      if (dot < -0.2F) {
        continue;
      }
      const float cross = std::abs(vx * ndy - vy * ndx);
      if (cross <= 0.35F) {
        e.hp -= t.damage;
        if (e.hp <= 0) {
          coins_ += 12;
        } else {
          hit_splats_.push_back({EnemyCell(e), 0.18F});
        }
      }
    }

    // Build beam cells for rendering until board edge.
    Beam b;
    float bx = static_cast<float>(center.x);
    float by = static_cast<float>(center.y);
    for (int i = 0; i < 120; ++i) {  // generous to cross the board
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

  void FireShockwave(const Tower& t) {
    const auto def = GetDef(t.type);
    Shockwave sw;
    sw.center = TowerCenter(t);
    sw.radius = 0.0F;
    sw.max_radius = def.range;
    sw.speed = 10.0F;
    sw.time_left = 0.45F;
    shockwaves_.push_back(sw);

    for (auto& e : enemies_) {
      const auto pos = EnemyCell(e);
      if (InRange(sw.center, pos, def.range + 0.2F)) {
        e.hp -= t.damage;
        if (e.hp <= 0) {
          coins_ += 12;
        } else {
          hit_splats_.push_back({pos, 0.22F});
        }
      }
    }
  }

  void UpdateShockwaves() {
    for (auto& sw : shockwaves_) {
      sw.radius += sw.speed * kTickSeconds;
      sw.time_left -= kTickSeconds;
    }
    shockwaves_.erase(
        std::remove_if(shockwaves_.begin(), shockwaves_.end(),
                       [](const Shockwave& sw) {
                         return sw.time_left <= 0.0F || sw.radius > sw.max_radius;
                       }),
        shockwaves_.end());
  }

  void UpdateBeams() {
    for (auto& b : beams_) {
      b.time_left -= kTickSeconds;
    }
    beams_.erase(
        std::remove_if(beams_.begin(), beams_.end(),
                       [](const Beam& b) { return b.time_left <= 0.0F; }),
        beams_.end());
  }

  ftxui::Element RenderBoard() const {
    std::vector<std::vector<char>> glyphs(
        kBoardHeight, std::vector<char>(kBoardWidth, ' '));
    std::vector<std::vector<ftxui::Color>> backgrounds(
        kBoardHeight,
        std::vector<ftxui::Color>(kBoardWidth, ftxui::Color::DarkGreen));
    std::vector<std::vector<ftxui::Color>> foregrounds(
        kBoardHeight, std::vector<ftxui::Color>(kBoardWidth, ftxui::Color::White));
    std::vector<std::vector<bool>> highlight(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    std::vector<std::vector<bool>> enemy_mask(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));

    // Range indicator: subtle for existing cats, brighter for cursor preview.
    std::vector<std::vector<bool>> range_hint_base(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    std::vector<std::vector<bool>> range_hint_preview(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    for (const auto& t : towers_) {
      const auto def = GetDef(t.type);
      if (!def.show_range) {
        continue;
      }
      const auto center = TowerCenter(t);
      for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
          Position cell{x, y};
          if (InRange(center, cell, t.range)) {
            range_hint_base[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
          }
        }
      }
    }
    const TowerDef preview_def = GetDef(selected_type_);
    if (preview_def.show_range) {
      const auto center = Position{cursor_.x + preview_def.size / 2,
                                   cursor_.y + preview_def.size / 2};
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

    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        if (path_mask_[yi][xi]) {
          backgrounds[yi][xi] = ftxui::Color::DarkGoldenrod;
          glyphs[yi][xi] = '.';
          foregrounds[yi][xi] = ftxui::Color::Black;
        }
      }
    }

    for (const auto& t : towers_) {
      const char glyph =
          t.type == Tower::Type::Thunder ? 'T' : (t.type == Tower::Type::Fat ? 'F' : 'C');
      const ftxui::Color bg =
          t.type == Tower::Type::Thunder ? ftxui::Color::Blue1
          : t.type == Tower::Type::Fat   ? ftxui::Color::DarkOliveGreen3
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

    for (const auto& e : enemies_) {
      const auto pos = EnemyCell(e);
      const auto yi = static_cast<size_t>(pos.y);
      const auto xi = static_cast<size_t>(pos.x);
      glyphs[yi][xi] = 'r';
      backgrounds[yi][xi] = ftxui::Color::Red3;
      foregrounds[yi][xi] = EnemyColor(e);
      enemy_mask[yi][xi] = true;
    }

    for (const auto& p : projectiles_) {
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

    for (const auto& b : beams_) {
      for (const auto& cell : b.cells) {
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

    for (const auto& sw : shockwaves_) {
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

    for (const auto& hs : hit_splats_) {
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

    const auto apply_tint = [&](size_t yi, size_t xi, const ftxui::Color& tint,
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

    const auto cy = static_cast<size_t>(cursor_.y);
    const auto cx = static_cast<size_t>(cursor_.x);
    glyphs[cy][cx] = glyphs[cy][cx] == ' ' ? '+' : glyphs[cy][cx];

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

    return vbox(std::move(rows));
  }

  ftxui::Element RenderStats() const {
    const std::string wave_text = wave_active_ ? "Wave " + std::to_string(wave_)
                                               : "Waiting";
    std::vector<ftxui::Element> lines;
    lines.push_back(text("Cat Burrow Defense"));
    lines.push_back(text("Status: " + wave_text));
    lines.push_back(text("Lives: " + std::to_string(lives_)));
    lines.push_back(text("Coins: " + std::to_string(coins_)));
    lines.push_back(text("Cats: " + std::to_string(towers_.size())));
    lines.push_back(separator());

    const auto selected_def = GetDef(selected_type_);
    lines.push_back(text("Selected: " + selected_def.name));
    lines.push_back(separator());

    const std::array<TowerDef, 3> defs = {GetDef(Tower::Type::Default),
                                          GetDef(Tower::Type::Thunder),
                                          GetDef(Tower::Type::Fat)};
    lines.push_back(text("Shop (press 1/2/3):"));
    for (const auto& d : defs) {
      std::string line = d.name + " - " + std::to_string(d.cost) + "c";
      if (d.type == Tower::Type::Thunder) {
        line += " | Laser, dmg " + std::to_string(d.damage) +
                ", slow fire";
      } else if (d.type == Tower::Type::Fat) {
        line += " | 2x2 AOE, dmg " + std::to_string(d.damage);
      } else {
        line += " | dmg " + std::to_string(d.damage);
      }
      lines.push_back(text(line));
    }

    if (game_over_) {
      lines.push_back(text("Game Over") | bold | color(ftxui::Color::RedLight));
    }

    lines.push_back(separator());
    lines.push_back(text("Controls:"));
    lines.push_back(text("Arrows/WASD - Move cursor"));
    lines.push_back(text("Space/C      - Place selected cat"));
    lines.push_back(text("1/2/3        - Select cat type"));
    lines.push_back(text("N            - Next wave"));
    lines.push_back(text("Q            - Quit"));

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
  Position cursor_{};

  std::mt19937 rng_{std::random_device{}()};

  Tower::Type selected_type_ = Tower::Type::Default;
  int coins_ = 0;
  int lives_ = 0;
  int wave_ = 0;
  bool wave_active_ = false;
  bool game_over_ = false;
  int spawn_remaining_ = 0;
  int spawn_cooldown_ms_ = 0;
};

class GameComponent : public ftxui::ComponentBase {
 public:
  explicit GameComponent(ftxui::ScreenInteractive& screen) : screen_(screen) {
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
  ftxui::ScreenInteractive& screen_;
  std::atomic<bool> running_{true};
  std::thread ticker_;
};

}  // namespace

int main() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto component = ftxui::Make<GameComponent>(screen);
  screen.Loop(component);
  return 0;
}
