#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "audio/audio.hpp"
#include "game.h"
#include "game/game_state.h"
#include "game/map_path.h"
#include "game/renderer.h"
#include "game/tower_combat.h"
#include "game/types.h"

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

class Game {
public:
  explicit Game(bool dev_mode = false)
      : dev_mode_(dev_mode),
        combat_(state_, map_path_, rng_,
                [this](std::string_view name) { Sfx(std::string(name)); }) {
    map_path_.Init();
    ResetState();
#ifdef ENABLE_AUDIO
    audio_ = std::make_unique<AudioSystem>();
    audio_->Init("audio.json");
    audio_->SetMusicForMap(state_.map_index);
#endif
  }

  void ResetState() {
    state_.Reset(dev_mode_);
    cursor_ = {3, kBoardHeight / 2};
    selected_type_ = Tower::Type::Default;
    view_shop_ = false;
    overlay_enabled_ = true;
    show_controls_ = false;
    rng_ = std::mt19937(std::random_device{}());
    map_path_.SetMap(state_.map_index);
#ifdef ENABLE_AUDIO
    if (audio_) {
      audio_->SetMusicForMap(state_.map_index);
    }
#endif
    intro_stage_ = IntroStage::Title;
  }

  void Tick() {
#ifdef ENABLE_AUDIO
    if (audio_)
      audio_->Update();
#endif
    if (warning_timer_ > 0.0F) {
      warning_timer_ = std::max(0.0F, warning_timer_ - Dt());
      if (warning_timer_ <= 0.0F) {
        warning_text_.clear();
      }
    }
    if (state_.game_over || state_.victory) {
      return;
    }

    SpawnTick();
    MoveEnemies();
    combat_.Act(Dt());
    MoveProjectiles();
    ResolveProjectiles();
    UpdateShockwaves();
    UpdateBeams();
    UpdateAreas();
    Cleanup();
    UpdateHitSplats();
    CheckWaveCompletion();
    if (state_.lives <= 0) {
      if (!state_.game_over) {
        state_.game_over = true;
        state_.auto_waves = false;
        SetMusic(-1);
      }
    }
  }

  bool HandleEvent(const ftxui::Event &event) {
    if ((state_.game_over || state_.victory) && event != ftxui::Event::Custom) {
      ResetState();
      return true;
    }

    if (!state_.game_over && !state_.victory && intro_stage_ != IntroStage::Playing &&
        event != ftxui::Event::Custom) {
      intro_stage_ = intro_stage_ == IntroStage::Title
                         ? IntroStage::Instructions
                         : IntroStage::Playing;
      return true;
    }

    if (state_.game_over) {
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
      state_.auto_waves = false;
      StartWave();
      handled = true;
    }
    if (event == ftxui::Event::Character('N')) {
      state_.auto_waves = true;
      if (!state_.wave_active) {
        StartWave();
      }
      handled = true;
    }
    if (event == ftxui::Event::Character('f')) {
      state_.fast_forward = !state_.fast_forward;
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
        audio_->SetMusicForMap(state_.map_index);
      }
#endif
      handled = true;
    }
    if (event == ftxui::Event::Escape) {
      view_shop_ = false;
      show_controls_ = false;
      if (state_.held_tower) {
        CancelHold();
        overlay_enabled_ = false;
      } else {
        overlay_enabled_ = false;
      }
      handled = true;
    }
    if (event == ftxui::Event::Character('m')) {
      if (state_.held_tower) {
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
    UIState ui;
    ui.cursor = cursor_;
    ui.selected_type = selected_type_;
    ui.overlay_enabled = overlay_enabled_;
    ui.show_controls = show_controls_;
    ui.view_shop = view_shop_;
    ui.dev_mode = dev_mode_;
    ui.warning_text = warning_text_;
    ui.warning_timer = warning_timer_;

    const bool intro = intro_stage_ != IntroStage::Playing;
    auto board = intro ? Renderer::BlankBoard() : renderer_.RenderBoard(state_, map_path_, ui);
    if (state_.game_over) {
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
          color(ftxui::Color::DarkRed) | bgcolor(ftxui::Color::Black) | bold |
          ftxui::center;
      auto overlay =
          ftxui::center(ftxui::vbox({
                            ftxui::filler(),
                            big_letters | border | bgcolor(ftxui::Color::Black),
                            ftxui::filler(),
                        }) |
                        ftxui::center);
      board = ftxui::dbox({board, overlay});
    } else if (state_.victory) {
      auto banner = ftxui::vbox({

                        // clang-format off
                  ftxui::text("                      .o8                              .o8   "),
                  ftxui::text(" .ooooo.   .oooo.   .o888oo       .ooooo.   .oooo.   .o888oo "),
                  ftxui::text("d88' `\"Y8 `P  )88b    888        d88' `\"Y8 `P  )88b    888   "),
                  ftxui::text("888        .oP\"888    888        888        .oP\"888    888   "),
                  ftxui::text("888   .o8 d8(  888    888 .      888   .o8 d8(  888    888 . "),
                  ftxui::text("`Y8bod8P' `Y888\"\"8o   \"888\"      `Y8bod8P' `Y888\"\"8o   \"888\" "),
                  ftxui::text(""),
                  ftxui::text("                V I C T O R Y "),
                  ftxui::text(""),

                        // clang-format on
                    }) |
                    color(ftxui::Color::GreenLight) |
                    bgcolor(ftxui::Color::DarkBlue) | bold | ftxui::center;
      auto msg = text("You cleared every map! Press any key to play again.") |
                 color(ftxui::Color::GreenLight) | ftxui::center;
      auto overlay =
          ftxui::center(ftxui::vbox({ftxui::filler(), banner | border, msg,
                                     ftxui::filler()}) |
                        ftxui::center | bgcolor(ftxui::Color::Black));
      board = ftxui::dbox({board, overlay});
    } else if (intro_stage_ == IntroStage::Title) {
      auto title =
          // clang-format off
          ftxui::vbox(
              {
                ftxui::text("                      .o8                              .o8   "),
                ftxui::text(" .ooooo.   .oooo.   .o888oo       .ooooo.   .oooo.   .o888oo "),
                ftxui::text("d88' `\"Y8 `P  )88b    888        d88' `\"Y8 `P  )88b    888   "),
                ftxui::text("888        .oP\"888    888        888        .oP\"888    888   "),
                ftxui::text("888   .o8 d8(  888    888 .      888   .o8 d8(  888    888 . "),
                ftxui::text("`Y8bod8P' `Y888\"\"8o   \"888\"      `Y8bod8P' `Y888\"\"8o   \"888\" "),
                ftxui::text(""),
                ftxui::text(""),
                ftxui::text(""),
                ftxui::text(""),
                ftxui::text(""),
                ftxui::text("            __..--''``---....___   _..._    __"),
                ftxui::text("    /// //_.-'    .-/\";  `        ``<._  ``.''_ `. / // /"),
                ftxui::text("   ///_.-' _..--.'_    \\                    `( ) ) // //"),
                ftxui::text("   / (_..-' // (< _     ;_..__               ; `' / ///"),
                ftxui::text("    / // // //  `-._,_)' // / ``--...____..-' /// / // ")}) |
          // clang-format on
          color(ftxui::Color::YellowLight) | bgcolor(ftxui::Color::Blue3) |
          bold | ftxui::center;
      auto subtitle = ftxui::text("don't let the vermin into your den!") |
                      color(ftxui::Color::YellowLight) | ftxui::center;
      auto overlay = ftxui::center(
          ftxui::vbox({ftxui::filler(),
                       title | border | bgcolor(ftxui::Color::Blue3), subtitle,
                       ftxui::filler()}) |
          ftxui::center | bgcolor(ftxui::Color::DarkBlue));
      board = ftxui::dbox({board, overlay});
    } else if (intro_stage_ == IntroStage::Instructions) {
      auto card =
          ftxui::vbox({
              text("how to play") | bold | color(ftxui::Color::YellowLight),
              separator(),
              text("Place cats with space/c. Start waves with n."),
              text("Earn kibbles, buy more cats, upgrade/sell."),
              text("Keep vermin from reaching your burrow."),
              text("Press h any time for the controls menu."),
              separator(),
              text("press any key to begin") |
                  color(ftxui::Color::YellowLight) | bold,
          }) |
          bgcolor(ftxui::Color::DarkBlue) | border |
          color(ftxui::Color::White) | ftxui::center;
      auto overlay =
          ftxui::center(ftxui::vbox({ftxui::filler(), card, ftxui::filler()}) |
                        ftxui::center);
      board = ftxui::dbox({board, overlay});
    }
    auto stats = renderer_.RenderStats(state_, ui);
    return hbox({
        board | border,
        separator(),
        stats | border,
    });
  }

  bool GameOver() const { return state_.game_over; }
  bool InIntro() const { return intro_stage_ != IntroStage::Playing; }

private:
  void StartWave() {
    if (state_.wave_active || state_.game_over) {
      return;
    }
    ++state_.wave;
    state_.spawn_remaining = 6 + DifficultyLevel() * 2;
    state_.spawn_cooldown_ms = 0;
    state_.wave_active = true;
    Sfx("wave_start");
  }

  void SpawnTick() {
    if (!state_.wave_active) {
      return;
    }

    if (state_.spawn_remaining <= 0 && state_.enemies.empty()) {
      return;
    }

    state_.spawn_cooldown_ms -= static_cast<int>(kTickMs * TimeScale());
    if (state_.spawn_cooldown_ms > 0 || state_.spawn_remaining <= 0) {
      return;
    }

    Enemy e;
    e.path_progress = 0.0F;
    const int diff = DifficultyLevel();
    e.type = SelectEnemyType(diff);
    ApplyEnemyStats(e, diff);
    const int width = std::max(1, map_path_.GetMap(state_.map_index).path_width);
    if (width > 1) {
      std::uniform_int_distribution<int> dist(-(width - 1), width - 1);
      e.lane_offset = dist(rng_);
    }
    state_.enemies.push_back(e);

    --state_.spawn_remaining;
    state_.spawn_cooldown_ms = static_cast<int>(600.0F / kSpeedFactor);
  }

  void MoveEnemies() {
    int lives_before = state_.lives;
    for (auto &e : state_.enemies) {
      if (e.sleep_timer > 0.0F) {
        e.sleep_timer = std::max(0.0F, e.sleep_timer - Dt());
        continue;
      }
      e.path_progress += e.speed * Dt();
    }

    for (auto &e : state_.enemies) {
      const int end_index = static_cast<int>(map_path_.Path().size() - 1);
      if (static_cast<int>(std::floor(e.path_progress)) >= end_index) {
        e.hp = 0;
        state_.lives = std::max(0, state_.lives - 1);
      }
    }
#ifdef ENABLE_AUDIO
    if (state_.lives < lives_before) {
      Sfx("life_lost");
    }
#endif
  }

  void MoveProjectiles() {
    for (auto &p : state_.projectiles) {
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
    survivors.reserve(state_.projectiles.size());
    for (auto &p : state_.projectiles) {
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
      for (size_t i = 0; i < state_.enemies.size(); ++i) {
        const auto pos = map_path_.EnemyCell(state_.enemies[i]);
        const float ddx = static_cast<float>(pos.x) - p.x;
        const float ddy = static_cast<float>(pos.y) - p.y;
        const float d2 = ddx * ddx + ddy * ddy;
        if (d2 < best_d2) {
          best_d2 = d2;
          hit_index = i;
        }
      }

      if (hit_index.has_value()) {
        auto &target = state_.enemies[*hit_index];
        target.hp -= p.damage;
        if (target.hp <= 0) {
          state_.kibbles += Bounty(target.type);
          PlayDeathSfx(target.type);
        } else {
          state_.hit_splats.push_back({map_path_.EnemyCell(target), 0.28F});
        }
      }
    }
    state_.projectiles = std::move(survivors);
  }

  void Cleanup() {
    state_.enemies.erase(std::remove_if(state_.enemies.begin(), state_.enemies.end(),
                                  [](const Enemy &e) { return e.hp <= 0; }),
                   state_.enemies.end());
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
    if (state_.map_index >= 3 && diff >= 16 && Rand(0.0F, 1.0F) < 0.08F) {
      return EnemyType::Dog;
    }
    return EnemyType::Rat;
  }

  void ApplyEnemyStats(Enemy &e, const int diff) {
    const float fDiff = static_cast<float>(diff);
    switch (e.type) {
    case EnemyType::Mouse:
      e.max_hp = 2 + diff * 1;
      e.speed = (0.95F + fDiff * 0.05F) * kSpeedFactor;
      break;
    case EnemyType::Rat:
      e.max_hp = 5 + static_cast<int>(fDiff * 2.5F);
      e.speed = (0.65F + fDiff * 0.065F) * kSpeedFactor;
      break;
    case EnemyType::BigRat:
      e.max_hp = 15 + diff * 4;
      e.speed = (0.55F + fDiff * 0.045F) * kSpeedFactor;
      break;
    case EnemyType::Dog:
      e.max_hp = 28 + diff * 6;
      e.speed = (0.9F + fDiff * 0.055F) * kSpeedFactor;
      break;
    }
    e.hp = e.max_hp;
  }

  void UpdateHitSplats() {
    for (auto &hs : state_.hit_splats) {
      hs.time_left -= Dt();
    }
    state_.hit_splats.erase(
        std::remove_if(state_.hit_splats.begin(), state_.hit_splats.end(),
                       [](const HitSplat &hs) { return hs.time_left <= 0.0F; }),
        state_.hit_splats.end());
  }

  void CheckWaveCompletion() {
    if (!state_.wave_active) {
      return;
    }
    if (state_.spawn_remaining > 0 || !state_.enemies.empty()) {
      return;
    }

    state_.wave_active = false;
    combat_.ReturnKittiesHome();
    state_.kibbles += 20 + state_.wave * 3;

    if (state_.wave % 10 == 0) {
      const bool last_map = state_.map_index == map_path_.MapCount() - 1;
      if (last_map) {
        state_.victory = true;
        state_.auto_waves = false;
        state_.wave_active = false;
        SetMusic(-1);
        return;
      }
      AdvanceMap();
    }

    if (state_.auto_waves && !state_.game_over) {
      StartWave();
    }
  }

  void AdvanceMap(bool dev_skip = false) {
    state_.map_index = (state_.map_index + 1) % map_path_.MapCount();
    state_.wave_active = false;
    state_.spawn_remaining = 0;
    state_.enemies.clear();
    state_.towers.clear();
    state_.held_tower.reset();
    // Preserve kibbles across maps to let players invest between stages.
    state_.lives = kStartingLives;
    state_.auto_waves = false;
    map_path_.SetMap(state_.map_index);
    if (dev_skip) {
      state_.wave = state_.map_index * 10;
    }
#ifdef ENABLE_AUDIO
    audio_->SetMusicForMap(state_.map_index);
    audio_->PlayEvent("map_change");
#endif
  }

  void PlaceTower() {
    if (!overlay_enabled_) {
      return;
    }
    if (state_.held_tower.has_value()) {
      TryPlaceHeld();
      return;
    }

    const TowerDef def = GetDef(selected_type_);
    if (!IsUnlocked(state_, def.type)) {
      return;
    }
    if (state_.kibbles < def.cost) {
      return;
    }
    if (def.type == Tower::Type::Catatonic &&
        CatatonicConflict(cursor_, def.size, def.type, def.range, false)) {
      ShowWarning("Can't place two sleeping cats within range of each "
                  "other.\nThey might wake each other up!",
                  4.0F);
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
    t.home = t.pos;
    state_.towers.push_back(t);
    state_.kibbles -= def.cost;
    Sfx("place");
  }

  float TimeScale() const {
    return state_.fast_forward ? kFastForwardMultiplier : 1.0F;
  }
  float Dt() const { return kTickSeconds * TimeScale(); }
  float Rand(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
  }

  void ShowWarning(const std::string &msg, float duration = 3.0F) {
    warning_text_ = msg;
    warning_timer_ = duration;
  }

  int DifficultyLevel() const {
    const int local = (state_.wave - 1) % 10 + 1;
    const int map_bonus = state_.map_index * 2; // Softer ramp to allow longer runs.
    return local + map_bonus;
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

  void Unlock(Tower::Type type) {
    if (type == Tower::Type::Fat)
      state_.unlocked_fat = true;
    if (type == Tower::Type::Kitty)
      state_.unlocked_kitty = true;
    if (type == Tower::Type::Thunder)
      state_.unlocked_thunder = true;
    if (type == Tower::Type::Catatonic)
      state_.unlocked_catatonic = true;
    if (type == Tower::Type::Galactic)
      state_.unlocked_galactic = true;
  }

  void TryUnlockOrSelect(Tower::Type type) {
    if (IsUnlocked(state_, type)) {
      selected_type_ = type;
      return;
    }
    const auto def = GetDef(type);
    const int unlock_cost = def.cost * 10;
    if (state_.kibbles >= unlock_cost) {
      state_.kibbles -= unlock_cost;
      Unlock(type);
      selected_type_ = type;
      Sfx("unlock");
    }
  }

  bool CatatonicConflict(const Position &p, int size, Tower::Type type,
                         float range, bool upgraded) const {
    if (type != Tower::Type::Catatonic) {
      return false;
    }
    const float candidate_range = range + (upgraded ? 0.8F : 0.0F);
    const Vec2 center = {
        static_cast<float>(p.x) + (static_cast<float>(size) - 1.0F) / 2.0F,
        static_cast<float>(p.y) + (static_cast<float>(size) - 1.0F) / 2.0F};
    for (const auto &t : state_.towers) {
      if (t.type != Tower::Type::Catatonic) {
        continue;
      }
      const Vec2 other = TowerCenter(t);
      const float dx = center.x - other.x;
      const float dy = center.y - other.y;
      const float dist2 = dx * dx + dy * dy;
      const float max_r =
          candidate_range + t.range + (t.upgraded ? 0.8F : 0.0F);
      if (dist2 <= max_r * max_r) {
        return true;
      }
    }
    return false;
  }

  bool CanPlace(const Position &p, int size, Tower::Type type, float range,
                bool upgraded) const {
    if (p.x < 0 || p.y < 0 || p.x + size - 1 >= kBoardWidth ||
        p.y + size - 1 >= kBoardHeight) {
      return false;
    }
    if (map_path_.OccupiesPath(p, size)) {
      return false;
    }
    if (OverlapsTower(state_, p, size)) {
      return false;
    }
    if (CatatonicConflict(p, size, type, range, upgraded)) {
      return false;
    }
    return true;
  }

  std::optional<size_t> TowerIndexAt(const Position &p) const {
    for (size_t i = 0; i < state_.towers.size(); ++i) {
      const auto &t = state_.towers[i];
      const int tx2 = t.pos.x + t.size - 1;
      const int ty2 = t.pos.y + t.size - 1;
      if (p.x >= t.pos.x && p.x <= tx2 && p.y >= t.pos.y && p.y <= ty2) {
        return i;
      }
    }
    return std::nullopt;
  }

  void PickUpTower() {
    if (state_.held_tower.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    HeldTower hold;
    hold.tower = state_.towers[*idx];
    hold.original = state_.towers[*idx].pos;
    state_.held_tower = hold;
    state_.towers.erase(state_.towers.begin() + static_cast<long>(*idx));
    overlay_enabled_ = true; // ensure placement cues visible while holding
  }

  void TryPlaceHeld() {
    if (!state_.held_tower.has_value()) {
      return;
    }
    auto t = state_.held_tower->tower;
    if (t.type == Tower::Type::Catatonic &&
        CatatonicConflict(cursor_, t.size, t.type, t.range, t.upgraded)) {
      ShowWarning("Can't place two sleeping cats within range of each "
                  "other.\nThey might wake each other up!",
                  4.0F);
      return;
    }
    if (!CanPlace(cursor_, t.size, t.type, t.range, t.upgraded)) {
      return;
    }
    t.pos = cursor_;
    t.home = t.pos;
    t.cooldown = Rand(0.05F, t.fire_rate);
    state_.towers.push_back(t);
    state_.held_tower.reset();
    overlay_enabled_ = false;
  }

  void CancelHold() {
    if (!state_.held_tower.has_value()) {
      return;
    }
    auto t = state_.held_tower->tower;
    t.pos = state_.held_tower->original;
    state_.towers.push_back(t);
    state_.held_tower.reset();
    overlay_enabled_ = false;
  }

  void SellTowerAtCursor() {
    if (state_.held_tower.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    const Tower &t = state_.towers[*idx];
    const auto def = GetDef(t.type);
    const int refund =
        static_cast<int>(std::round(static_cast<float>(def.cost) * 0.6F));
    state_.kibbles += refund;
    state_.towers.erase(state_.towers.begin() + static_cast<long>(*idx));
    Sfx("sell");
  }

  void UpgradeTowerAtCursor() {
    if (state_.held_tower.has_value()) {
      return;
    }
    const auto idx = TowerIndexAt(cursor_);
    if (!idx.has_value()) {
      return;
    }
    Tower &t = state_.towers[*idx];
    if (t.upgraded) {
      return;
    }
    const auto def = GetDef(t.type);
    const int cost = def.cost * 2;
    if (state_.kibbles < cost) {
      return;
    }
    state_.kibbles -= cost;
    t.upgraded = true;
    if (t.type == Tower::Type::Fat) {
      t.range += 1.0F;
    } else if (t.type == Tower::Type::Default) {
      t.range += 2.0F;
    }
    Sfx("unlock");
  }

  void UpdateShockwaves() {
    for (auto &sw : state_.shockwaves) {
      sw.radius += sw.speed * Dt();
      sw.time_left -= Dt();
    }
    state_.shockwaves.erase(std::remove_if(state_.shockwaves.begin(), state_.shockwaves.end(),
                                     [](const Shockwave &sw) {
                                       return sw.time_left <= 0.0F ||
                                              sw.radius > sw.max_radius;
                                     }),
                      state_.shockwaves.end());
  }

  void UpdateBeams() {
    for (auto &b : state_.beams) {
      b.time_left -= Dt();
    }
    state_.beams.erase(
        std::remove_if(state_.beams.begin(), state_.beams.end(),
                       [](const Beam &b) { return b.time_left <= 0.0F; }),
        state_.beams.end());
  }

  void UpdateAreas() {
    for (auto &a : state_.area_highlights) {
      a.time_left -= Dt();
    }
    state_.area_highlights.erase(std::remove_if(state_.area_highlights.begin(),
                                          state_.area_highlights.end(),
                                          [](const AreaHighlight &a) {
                                            return a.time_left <= 0.0F;
                                          }),
                           state_.area_highlights.end());
  }

  bool dev_mode_ = false;
  GameState state_;
  MapPath map_path_;
  std::mt19937 rng_{std::random_device{}()};
  std::unique_ptr<AudioSystem> audio_;
  TowerCombat combat_;
  Renderer renderer_;
  Position cursor_{};

  Tower::Type selected_type_ = Tower::Type::Default;
  bool view_shop_ = false;
  bool overlay_enabled_ = true;
  bool show_controls_ = false;
  IntroStage intro_stage_ = IntroStage::Title;
  std::string warning_text_;
  float warning_timer_ = 0.0F;
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
    if (event == ftxui::Event::Character('q') && !game_.InIntro() &&
        !game_.GameOver()) {
      quit_presses_++;
      if (quit_presses_ >= 3) {
        running_ = false;
        screen_.Exit();
      }
      return true;
    }

    if (event == ftxui::Event::Custom) {
      game_.Tick();
      return true;
    }

    quit_presses_ = 0;
    return game_.HandleEvent(event);
  }

private:
  Game game_;
  ftxui::ScreenInteractive &screen_;
  std::atomic<bool> running_{true};
  std::thread ticker_;
  int quit_presses_ = 0;
};

} // namespace

ftxui::Component MakeGameComponent(ftxui::ScreenInteractive &screen,
                                   bool dev_mode) {
  return ftxui::Make<GameComponent>(screen, dev_mode);
}
