#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>

constexpr int kBoardWidth = 48;
constexpr int kBoardHeight = 28;
constexpr int kTickMs = 16; // ~60 FPS
constexpr float kTickSeconds = kTickMs / 1000.0F;
constexpr int kStartingKibbles = 90;
constexpr float kSpeedFactor = 1.3F; // Global pacing multiplier (~30% faster).
constexpr float kFastForwardMultiplier = 5.0F;
constexpr int kStartingLives = 9;
constexpr float kCatSleepBase = 0.5F;
constexpr float kCatSleepUpgrade = 1.0F;
constexpr float kCatSleepCap = 5.0F;
constexpr float kGalacticVoidChance = 0.50;
constexpr float kGalacticVoidBackstep = 8.0F;
constexpr float kKittyJumpBonusRange = 1.5F; // extra reach for upgraded jumps

struct Position {
  int x = 0;
  int y = 0;
};

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

inline float DistanceSquared(const Vec2 &a, const Position &b) {
  const float dx = a.x - static_cast<float>(b.x);
  const float dy = a.y - static_cast<float>(b.y);
  return dx * dx + dy * dy;
}

inline std::vector<Position> KittyOverlayCells(const Vec2 &center) {
  std::vector<Position> cells;
  const std::array<std::pair<int, int>, 4> dirs = {
      std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (const auto &[px, py] : dirs) {
    const int perp_x = -py;
    const int perp_y = px;
    const std::array<int, 3> offs = {-1, 0, 1}; // overlapping 2-wide bands
    for (int step = 1; step <= 3; ++step) {
      for (int off : offs) {
        const int gx =
            static_cast<int>(std::round(center.x)) + px * step + perp_x * off;
        const int gy =
            static_cast<int>(std::round(center.y)) + py * step + perp_y * off;
        if (gx < 0 || gy < 0 || gx >= kBoardWidth || gy >= kBoardHeight) {
          continue;
        }
        cells.push_back({gx, gy});
      }
    }
  }
  return cells;
}

inline ftxui::Color BlendColor(const ftxui::Color &base,
                               const ftxui::Color &overlay, float alpha) {
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
  Position home{};
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

enum class IntroStage { Title, Instructions, Playing };

inline Vec2 TowerCenterAt(const Position &p, int size) {
  const float cx =
      static_cast<float>(p.x) + (static_cast<float>(size) - 1.0F) / 2.0F;
  const float cy =
      static_cast<float>(p.y) + (static_cast<float>(size) - 1.0F) / 2.0F;
  return {cx, cy};
}

inline Vec2 TowerCenter(const Tower &t) { return TowerCenterAt(t.pos, t.size); }

inline bool InRange(const Vec2 &center, const Position &cell, float range) {
  return DistanceSquared(center, cell) <= range * range;
}

inline int Bounty(EnemyType type) {
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

inline std::string PadRight(const std::string &s, size_t w) {
  if (s.size() >= w)
    return s;
  return s + std::string(w - s.size(), ' ');
}

inline int TypeKey(Tower::Type t) {
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

inline TowerDef GetDef(Tower::Type type) {
  switch (type) {
  case Tower::Type::Default:
    return {type, "Default Cat", 35, 3, 4.5F, 0.85F, true, 1};
  case Tower::Type::Fat:
    return {type, "Fat Cat", 35, 4, 2.4F, 1.4F, true, 2};
  case Tower::Type::Kitty:
    return {type, "Kitty Cat", 50, 3, 3.0F, 1.0F, true, 1};
  case Tower::Type::Thunder:
    return {type, "Thundercat", 100, 6, 999.0F, 2.6F, false, 1};
  case Tower::Type::Catatonic:
    return {type, "Catatonic", 150, 2, 3.2F, 2.2F, true, 1};
  case Tower::Type::Galactic:
    return {type, "Galacticat", 200, 20, 7.5F, 2.5F, true, 1};
  }
  return {Tower::Type::Default, "Default Cat", 35, 3, 3.5F, 0.85F, true, 1};
}

inline std::vector<TowerDef> SortedDefs() {
  std::vector<TowerDef> defs = {
      GetDef(Tower::Type::Default),   GetDef(Tower::Type::Fat),
      GetDef(Tower::Type::Kitty),     GetDef(Tower::Type::Thunder),
      GetDef(Tower::Type::Catatonic), GetDef(Tower::Type::Galactic)};
  std::sort(defs.begin(), defs.end(), [](const TowerDef &a, const TowerDef &b) {
    if (a.cost == b.cost)
      return a.name < b.name;
    return a.cost < b.cost;
  });
  return defs;
}

struct UIState {
  Position cursor{};
  Tower::Type selected_type = Tower::Type::Default;
  bool overlay_enabled = true;
  bool show_controls = false;
  bool view_shop = false;
  bool dev_mode = false;
  std::string warning_text;
  float warning_timer = 0.0F;
};
