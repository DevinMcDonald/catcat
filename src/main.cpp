#include <algorithm>
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
constexpr int kTickMs = 120;
constexpr float kTickSeconds = kTickMs / 1000.0F;
constexpr int kStartingCoins = 90;
constexpr int kStartingLives = 15;
constexpr int kCatCost = 35;

struct Position {
  int x = 0;
  int y = 0;
};

float DistanceSquared(const Position& a, const Position& b) {
  const float dx = static_cast<float>(a.x - b.x);
  const float dy = static_cast<float>(a.y - b.y);
  return dx * dx + dy * dy;
}

struct Enemy {
  float path_progress = 0.0F;  // index along path cells
  float speed = 1.0F;          // cells per second
  int hp = 1;
  int max_hp = 1;
};

struct Tower {
  Position pos{};
  int damage = 2;
  float range = 3.2F;
  float cooldown = 0.0F;       // time until next shot
  float fire_rate = 1.2F;      // seconds between shots
};

struct HitSplat {
  Position pos{};
  float time_left = 0.25F;  // seconds
};

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

    for (size_t i = 0; i < enemies_.size(); ++i) {
      if (enemies_[i].hp <= 0) {
        continue;
      }
      const auto pos = EnemyCell(enemies_[i]);
      const float d2 = DistanceSquared(t.pos, pos);
      if (d2 > range2) {
        continue;
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

      auto& target = enemies_[*target_index];
      target.hp -= t.damage;
      t.cooldown = t.fire_rate;
      if (target.hp <= 0) {
        coins_ += 12;
      } else {
        const auto pos = EnemyCell(target);
        hit_splats_.push_back({pos, 0.28F});
      }
    }
  }

  void Cleanup() {
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [](const Enemy& e) { return e.hp <= 0; }),
                   enemies_.end());
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
    if (coins_ < kCatCost) {
      return;
    }
    if (cursor_.x < 0 || cursor_.x >= kBoardWidth || cursor_.y < 0 ||
        cursor_.y >= kBoardHeight) {
      return;
    }
    const auto cy = static_cast<size_t>(cursor_.y);
    const auto cx = static_cast<size_t>(cursor_.x);
    if (path_mask_[cy][cx]) {
      return;
    }
    for (const auto& t : towers_) {
      if (t.pos.x == cursor_.x && t.pos.y == cursor_.y) {
        return;
      }
    }

    Tower t;
    t.pos = cursor_;
    t.damage = 3;
    t.range = 3.5F;
    t.fire_rate = 1.0F;
    towers_.push_back(t);
    coins_ -= kCatCost;
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

    // Range indicator: subtle for existing cats, brighter for cursor preview.
    std::vector<std::vector<bool>> range_hint_base(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    std::vector<std::vector<bool>> range_hint_preview(
        kBoardHeight, std::vector<bool>(kBoardWidth, false));
    const Tower* hovered_tower = nullptr;
    for (const auto& t : towers_) {
      if (t.pos.x == cursor_.x && t.pos.y == cursor_.y) {
        hovered_tower = &t;
        break;
      }
    }
    for (const auto& t : towers_) {
      for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
          Position cell{x, y};
          if (InRange(t.pos, cell, t.range)) {
            range_hint_base[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
          }
        }
      }
    }
    const float preview_range =
        hovered_tower ? hovered_tower->range : 3.5F;  // default cat range
    for (int y = 0; y < kBoardHeight; ++y) {
      for (int x = 0; x < kBoardWidth; ++x) {
        Position cell{x, y};
        if (InRange(cursor_, cell, preview_range)) {
          range_hint_preview[static_cast<size_t>(y)]
                             [static_cast<size_t>(x)] = true;
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
      const auto yi = static_cast<size_t>(t.pos.y);
      const auto xi = static_cast<size_t>(t.pos.x);
      glyphs[yi][xi] = 'C';
      backgrounds[yi][xi] = ftxui::Color::Gold1;
      foregrounds[yi][xi] = ftxui::Color::Black;
      highlight[yi][xi] = true;
    }

    for (const auto& e : enemies_) {
      const auto pos = EnemyCell(e);
      const auto yi = static_cast<size_t>(pos.y);
      const auto xi = static_cast<size_t>(pos.x);
      glyphs[yi][xi] = 'r';
      backgrounds[yi][xi] = ftxui::Color::Red3;
      foregrounds[yi][xi] = EnemyColor(e);
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

    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        if (range_hint_base[yi][xi]) {
          backgrounds[yi][xi] = ftxui::Color::DarkSeaGreen;
        }
        if (range_hint_preview[yi][xi]) {
          backgrounds[yi][xi] = ftxui::Color::LightSkyBlue1;
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
    lines.push_back(text("Default Cat:"));
    lines.push_back(text("- Damage: 3, Range: 3.5"));
    lines.push_back(text("- Fire Rate: 1.0s, Cost: " + std::to_string(kCatCost)));

    if (game_over_) {
      lines.push_back(text("Game Over") | bold | color(ftxui::Color::RedLight));
    }

    lines.push_back(separator());
    lines.push_back(text("Controls:"));
    lines.push_back(text("Arrows/WASD - Move cursor"));
    lines.push_back(text("Space/C      - Place cat (" + std::to_string(kCatCost) + ")"));
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
  Position cursor_{};

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
