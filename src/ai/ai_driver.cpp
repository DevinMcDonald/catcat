#include "ai/ai_driver.h"

#include <algorithm>
#include <cmath>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

using ftxui::bold;
using ftxui::color;
using ftxui::hbox;
using ftxui::separator;
using ftxui::text;
using ftxui::vbox;

// ── Softmax helper ────────────────────────────────────────────────────────────
static std::vector<float> Softmax(const std::vector<float>& logits,
                                  const std::array<bool, kAINumActions>& mask) {
  std::vector<float> probs(logits.size(), 0.0f);
  float max_l = -1e30f;
  for (int i = 0; i < kAINumActions; ++i)
    if (mask[static_cast<std::size_t>(i)]) max_l = std::max(max_l, logits[static_cast<std::size_t>(i)]);

  float sum = 0.0f;
  for (int i = 0; i < kAINumActions; ++i) {
    if (mask[static_cast<std::size_t>(i)]) {
      probs[static_cast<std::size_t>(i)] = std::exp(logits[static_cast<std::size_t>(i)] - max_l);
      sum += probs[static_cast<std::size_t>(i)];
    }
  }
  if (sum > 0.0f)
    for (auto& p : probs) p /= sum;
  return probs;
}

// ── AIPlayer ──────────────────────────────────────────────────────────────────
AIPlayer::AIPlayer(int seed)
    : net_(kAIObs, 64, 32, kAINumActions),
      rng_(static_cast<std::mt19937_64::result_type>(seed)) {}

bool AIPlayer::Load(const std::string& path) { return net_.Load(path); }
bool AIPlayer::Save(const std::string& path) const { return net_.Save(path); }

AIPlayer AIPlayer::Perturbed(float sigma) const {
  AIPlayer copy = *this;
  copy.net_ = net_.Perturbed(sigma);
  return copy;
}

AICommand AIPlayer::SelectAction(const AIObservation& obs) const {
  const std::vector<float> input(obs.features.begin(), obs.features.end());
  const std::vector<float> logits = net_.Forward(input);
  const std::vector<float> probs  = Softmax(logits, obs.valid);

  // Check if any valid action exists
  bool any_valid = false;
  for (bool v : obs.valid) if (v) { any_valid = true; break; }
  if (!any_valid) return AICommand{kAIActNoop};

  std::discrete_distribution<int> dist(probs.begin(), probs.end());
  return AICommand{dist(rng_)};
}

// ── Trainer ───────────────────────────────────────────────────────────────────
Trainer::Trainer(const std::string& weights_path, bool fresh)
    : best_player_(42),
      best_fitness_(-1e9f),
      sigma_(0.1f),
      success_count_(0),
      eval_count_(0),
      weights_path_(weights_path) {
  if (!fresh) best_player_.Load(weights_path);
}

float Trainer::EvaluatePlayer(const AIPlayer& player,
    std::array<int, kAINumTowerTypes>* out_tower_counts,
    std::array<int, kAINumTowerTypes>* out_upgrade_counts,
    std::array<int, kAINumTowerTypes>* out_damage,
    int* out_wins, int* out_losses, int* out_waves,
    int* out_ticks) const {
  float total = 0.0f;
  for (int g = 0; g < kEvalGames; ++g) {
    GameAIBridge game;
    int ticks_since_decision = 0;
    int game_ticks = 0;
    while (!game.IsTerminal()) {
      game.Tick();
      ++ticks_since_decision;
      ++game_ticks;
      if (ticks_since_decision >= GameAIBridge::kDecisionInterval) {
        ticks_since_decision = 0;
        const AIObservation obs = game.Observe();
        game.Act(player.SelectAction(obs));
      }
    }
    if (out_ticks) *out_ticks += game_ticks;
    const AIEpisodeResult res = game.GetResult();
    total += res.fitness;
    if (out_tower_counts)
      for (int i = 0; i < kAINumTowerTypes; ++i)
        (*out_tower_counts)[static_cast<std::size_t>(i)] +=
            res.tower_type_counts[static_cast<std::size_t>(i)];
    if (out_upgrade_counts)
      for (int i = 0; i < kAINumTowerTypes; ++i)
        (*out_upgrade_counts)[static_cast<std::size_t>(i)] +=
            res.tower_upgrade_counts[static_cast<std::size_t>(i)];
    if (out_damage)
      for (int i = 0; i < kAINumTowerTypes; ++i)
        (*out_damage)[static_cast<std::size_t>(i)] +=
            res.tower_damage_dealt[static_cast<std::size_t>(i)];
    if (out_wins)   *out_wins   += res.victory ? 1 : 0;
    if (out_losses) *out_losses += res.victory ? 0 : 1;
    if (out_waves)  *out_waves  += res.waves_cleared;
  }
  return total / static_cast<float>(kEvalGames);
}

float Trainer::RunBatch(AIStats& stats) {
  float batch_best = -1e9f;
  std::array<int, kAINumTowerTypes> batch_tower_counts{};
  std::array<int, kAINumTowerTypes> batch_upgrade_counts{};
  std::array<int, kAINumTowerTypes> batch_damage{};
  int batch_wins = 0, batch_losses = 0, batch_ticks = 0;
  int best_cand_waves = 0;
  float best_cand_fitness = -1e9f;

  for (int c = 0; c < kCandidates; ++c) {
    const AIPlayer candidate = best_player_.Perturbed(sigma_);
    int cand_waves = 0, cand_ticks = 0;
    const float fitness = EvaluatePlayer(candidate, &batch_tower_counts, &batch_upgrade_counts,
                                         &batch_damage, &batch_wins, &batch_losses,
                                         &cand_waves, &cand_ticks);
    batch_ticks += cand_ticks;
    ++eval_count_;

    stats.episodes.fetch_add(kEvalGames);
    stats.last_fitness.store(fitness);
    stats.PushHistory(fitness);

    if (fitness > best_cand_fitness) {
      best_cand_fitness = fitness;
      best_cand_waves   = cand_waves;
    }
    if (fitness > best_fitness_) {
      best_player_ = candidate;
      best_fitness_ = fitness;
      best_player_.Save(weights_path_);
      stats.best_fitness.store(best_fitness_);
      ++success_count_;
    }
    batch_best = std::max(batch_best, fitness);
  }

  // Accumulate tower usage — never resets so bars only grow.
  for (int i = 0; i < kAINumTowerTypes; ++i) {
    stats.tower_counts[static_cast<std::size_t>(i)].fetch_add(
        batch_tower_counts[static_cast<std::size_t>(i)]);
    stats.upgrade_counts[static_cast<std::size_t>(i)].fetch_add(
        batch_upgrade_counts[static_cast<std::size_t>(i)]);
  }
  stats.wins.fetch_add(batch_wins);
  stats.losses.fetch_add(batch_losses);
  stats.PushWinRate(batch_wins, batch_wins + batch_losses);
  stats.PushWaves(static_cast<float>(best_cand_waves) / static_cast<float>(kEvalGames));
  // Compute DPS per tower (average damage/sec per individual placed tower).
  // Divide aggregate damage by total game time, then by average towers-per-game.
  constexpr float kSecsPerTick = 1.0f / 60.0f;
  const float batch_time = static_cast<float>(batch_ticks) * kSecsPerTick;
  const int   num_games  = kCandidates * kEvalGames;
  for (int i = 0; i < kAINumTowerTypes; ++i) {
    const int   count = batch_tower_counts[static_cast<std::size_t>(i)];
    const float dps   = (batch_time > 0.0f && count > 0)
        ? static_cast<float>(batch_damage[static_cast<std::size_t>(i)]) * static_cast<float>(num_games)
          / (batch_time * static_cast<float>(count))
        : 0.0f;
    stats.last_batch_dps[static_cast<std::size_t>(i)].store(dps);
  }

  // 1/5 success rule: adjust sigma every 5 evaluations
  if (eval_count_ % 5 == 0) {
    if (success_count_ > 1) sigma_ *= 0.82f;
    else                    sigma_ = std::min(sigma_ * 1.22f, 0.5f);
    success_count_ = 0;
  }
  stats.sigma.store(sigma_);
  return batch_best;
}

// ── FTXUI AI component ────────────────────────────────────────────────────────
namespace {

constexpr int kTickMs            = 16;
constexpr int kDisplayFastSteps  = 5;  // game ticks per display tick in fast mode

class AIGameComponent : public ftxui::ComponentBase {
public:
  AIGameComponent(ftxui::ScreenInteractive& screen,
                  const std::string& weights_path,
                  bool fast_forward,
                  bool fresh)
      : screen_(screen), fast_display_(fast_forward) {
    display_game_ = std::make_unique<GameAIBridge>(/*headless=*/false);

    training_thread_ = std::thread([this, weights_path, fresh] {
      Trainer trainer(weights_path, fresh);
      // Seed the display player from whatever weights were loaded.
      {
        std::lock_guard<std::mutex> lock(player_mutex_);
        display_player_ = trainer.BestPlayer();
      }
      while (running_) {
        trainer.RunBatch(stats_);
        std::lock_guard<std::mutex> lock(player_mutex_);
        display_player_ = trainer.BestPlayer();
      }
    });

    ticker_ = std::thread([this] {
      while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
        bool expected = false;
        if (tick_pending_.compare_exchange_strong(expected, true))
          screen_.Post(ftxui::Event::Custom);
      }
    });
  }

  ~AIGameComponent() override {
    running_ = false;
    if (training_thread_.joinable()) training_thread_.join();
    if (ticker_.joinable()) ticker_.join();
  }

  // ── Render ──────────────────────────────────────────────────────────────────
  ftxui::Element Render() override {
    return hbox({
      display_game_render_,
      separator(),
      RenderStatsSidebar(),
    });
  }

  bool OnEvent(ftxui::Event event) override {
    if (event == ftxui::Event::Character('q')) {
      if (++quit_presses_ >= 3) { running_ = false; screen_.Exit(); }
      return true;
    }
    if (event == ftxui::Event::Special("\x03")) {
      running_ = false;
      screen_.Exit();
      return true;
    }
    if (event == ftxui::Event::Character('f')) {
      fast_display_ = !fast_display_;
      return true;
    }
    if (event == ftxui::Event::Character('t')) {
      display_game_->ToggleSfx();
      return true;
    }
    if (event == ftxui::Event::Character('y')) {
      display_game_->ToggleMusic();
      return true;
    }
    if (event != ftxui::Event::Custom) return false;
    tick_pending_.store(false);
    TickDisplay();
    return true;
  }

private:
  // ── Per-tick display logic ───────────────────────────────────────────────
  void TickDisplay() {
    // If we're in the post-game-over pause, just count down and freeze the render.
    if (display_game_over_ticks_ > 0) {
      --display_game_over_ticks_;
      if (display_game_over_ticks_ == 0) {
        prev_display_counts_   = {};
        prev_display_upgrades_ = {};
        display_game_ = std::make_unique<GameAIBridge>(/*headless=*/false);
        display_ticks_since_decision_ = 0;
      }
      return;
    }

    const int steps = fast_display_ ? kDisplayFastSteps : 1;
    for (int s = 0; s < steps; ++s) {
      display_game_->Tick();
      ++display_ticks_since_decision_;
      if (display_ticks_since_decision_ >= GameAIBridge::kDecisionInterval) {
        display_ticks_since_decision_ = 0;
        const AIObservation obs = display_game_->Observe();
        AICommand cmd;
        {
          std::lock_guard<std::mutex> lock(player_mutex_);
          cmd = display_player_.SelectAction(obs);
        }
        display_game_->Act(cmd);
      }
      if (display_game_->IsTerminal()) break;
    }

    display_game_render_ = display_game_->Render();

    // Accumulate any newly-placed/upgraded towers from the display game since last tick.
    const auto res = display_game_->GetResult();
    for (int i = 0; i < kAINumTowerTypes; ++i) {
      const std::size_t idx = static_cast<std::size_t>(i);
      const int place_delta = res.tower_type_counts[idx] - prev_display_counts_[idx];
      if (place_delta > 0) {
        stats_.tower_counts[idx].fetch_add(place_delta);
        prev_display_counts_[idx] = res.tower_type_counts[idx];
      }
      const int upgrade_delta = res.tower_upgrade_counts[idx] - prev_display_upgrades_[idx];
      if (upgrade_delta > 0) {
        stats_.upgrade_counts[idx].fetch_add(upgrade_delta);
        prev_display_upgrades_[idx] = res.tower_upgrade_counts[idx];
      }
    }

    if (display_game_->IsTerminal()) {
      if (display_game_->GetResult().victory) stats_.wins.fetch_add(1);
      else                                    stats_.losses.fetch_add(1);
      // Pause on the game-over screen for ~5 seconds before resetting.
      constexpr int kGameOverPauseTicks = 5000 / 16;
      display_game_over_ticks_ = kGameOverPauseTicks;
    }
  }

  // ── Rendering helpers ────────────────────────────────────────────────────
  ftxui::Element RenderStatsSidebar() const {
    using ftxui::Color;
    std::vector<ftxui::Element> lines;

    // Right-pad / left-pad to fixed display width
    auto rpad = [](std::string s, int w) -> std::string {
      const int p = w - static_cast<int>(s.size());
      if (p > 0) s.append(static_cast<std::size_t>(p), ' ');
      return s;
    };
    auto lpad = [](std::string s, int w) -> std::string {
      const int p = w - static_cast<int>(s.size());
      if (p > 0) s.insert(0, static_cast<std::size_t>(p), ' ');
      return s;
    };
    auto fmt = [](float v, int prec) -> std::string {
      std::ostringstream o; o << std::fixed << std::setprecision(prec) << v;
      return o.str();
    };
    // Dimmed label + bold value, label padded to fixed width
    auto stat_row = [&](const std::string& lbl, const std::string& val) -> ftxui::Element {
      return hbox({text(rpad(" " + lbl, 13)) | color(Color::GrayLight), text(val) | bold});
    };
    // Build sparkline from a ring buffer.
    // fixed_lo/fixed_hi: fixed scale (-1 = auto-detect from data).
    // n_points: how many trailing entries to render (AIStats::kHistLen = all).
    // Returns {sparkline_string, most_recent_value}
    auto make_spark = [&](const std::array<std::atomic<float>, AIStats::kHistLen>& hist,
                          const std::atomic<int>& hd,
                          float fixed_lo, float fixed_hi,
                          int n_points = AIStats::kHistLen) -> std::pair<std::string, float> {
      static constexpr const char* kB[] = {" ","▁","▂","▃","▄","▅","▆","▇","█"};
      const int h = hd.load();
      const int n = std::clamp(n_points, 1, AIStats::kHistLen);
      // Load all values; real data occupies the last n slots of the ordered array.
      std::array<float, AIStats::kHistLen> vals{};
      for (int i = 0; i < AIStats::kHistLen; ++i) {
        const int idx = (h + i) % AIStats::kHistLen;
        vals[static_cast<std::size_t>(i)] = hist[static_cast<std::size_t>(idx)].load();
      }
      // Determine scale from the n visible entries only.
      float minv = 1e9f, maxv = -1e9f;
      for (int i = AIStats::kHistLen - n; i < AIStats::kHistLen; ++i) {
        minv = std::min(minv, vals[static_cast<std::size_t>(i)]);
        maxv = std::max(maxv, vals[static_cast<std::size_t>(i)]);
      }
      const float lo    = fixed_lo >= 0.f ? fixed_lo : minv;
      const float hi    = fixed_hi >= 0.f ? fixed_hi : maxv;
      const float range = std::max(hi - lo, 0.01f);
      std::string spark;
      for (int i = AIStats::kHistLen - n; i < AIStats::kHistLen; ++i) {
        const float norm = std::clamp((vals[static_cast<std::size_t>(i)] - lo) / range, 0.f, 1.f);
        spark += kB[static_cast<int>(norm * 8.f)];
      }
      const int last = (h + AIStats::kHistLen - 1) % AIStats::kHistLen;
      return {spark, hist[static_cast<std::size_t>(last)].load()};
    };

    // ── Header ───────────────────────────────────────────────────────────
    lines.push_back(text(" AI Training ") | bold | color(Color::Cyan1));
    lines.push_back(separator());

    // ── Training stats ───────────────────────────────────────────────────
    lines.push_back(stat_row("Speed",    fast_display_ ? "5x  [f]" : "1x  [f]"));
    lines.push_back(stat_row("Episodes", std::to_string(stats_.episodes.load())));
    {
      const int w = stats_.wins.load(), l = stats_.losses.load();
      const int total = w + l;
      std::string val = std::to_string(w) + "W / " + std::to_string(l) + "L";
      if (total > 0)
        val += "  (" + fmt(100.f * static_cast<float>(w) / static_cast<float>(total), 1) + "%)";
      lines.push_back(stat_row("W / L", val));
    }
    lines.push_back(separator());
    {
      const float best = stats_.best_fitness.load();
      lines.push_back(stat_row("Best fit", best < -1e8f ? "--" : fmt(best, 1)));
    }
    lines.push_back(stat_row("Last fit", fmt(stats_.last_fitness.load(), 1)));
    lines.push_back(stat_row("Sigma",    fmt(stats_.sigma.load(), 3)));

    // ── Sparklines ───────────────────────────────────────────────────────
    lines.push_back(separator());
    {
      auto [spark, cur] = make_spark(stats_.win_rate_history, stats_.win_rate_head, 0.f, 1.f);
      lines.push_back(hbox({text(rpad(" win rate", 13)) | color(Color::GrayLight),
                            text(fmt(cur * 100.f, 1) + "%") | bold}));
      lines.push_back(text(" " + spark) | color(Color::Yellow1));
    }
    {
      auto [spark, cur] = make_spark(stats_.history, stats_.history_head, -1.f, -1.f);
      lines.push_back(hbox({text(rpad(" fitness", 13)) | color(Color::GrayLight),
                            text(fmt(cur, 1)) | bold}));
      lines.push_back(text(" " + spark) | color(Color::Green1));
    }
    {
      auto [spark, cur] = make_spark(stats_.waves_history, stats_.waves_head, 0.f, 100.f,
                                     stats_.waves_count.load());
      lines.push_back(hbox({text(rpad(" waves", 13)) | color(Color::GrayLight),
                            text(fmt(cur, 1)) | bold}));
      lines.push_back(text(" " + spark) | color(Color::Cyan1));
    }

    // ── Tower mix ────────────────────────────────────────────────────────
    lines.push_back(separator());
    lines.push_back(text(" tower mix") | color(Color::GrayLight));

    struct TowerInfo { const char* name; Color col; };
    static const TowerInfo kInfo[kAINumTowerTypes] = {
      {"Default Cat",  Color::Gold1          },
      {"Fat Cat",      Color::DarkOliveGreen3},
      {"Kitty Cat",    Color::Pink1          },
      {"Thundercat",   Color::Blue1          },
      {"Catatonic",    Color::Purple         },
      {"Catastrophe",  Color::DarkKhaki      },
      {"Galacticat",   Color::LightSteelBlue },
    };

    // Tower costs — must stay in sync with GetDef() in game.cpp
    static constexpr int kCost[kAINumTowerTypes] = {35, 35, 50, 100, 150, 175, 200};

    // Load last-batch DPS values and compute efficiency = DPS / cost for color grading.
    float dps_val[kAINumTowerTypes] = {};
    float eff[kAINumTowerTypes]     = {};
    float max_dps = 0.001f, min_eff = 1e9f, max_eff = -1e9f;
    for (int i = 0; i < kAINumTowerTypes; ++i) {
      dps_val[i] = stats_.last_batch_dps[static_cast<std::size_t>(i)].load();
      max_dps = std::max(max_dps, dps_val[i]);
      if (dps_val[i] > 0.0f) {
        eff[i] = dps_val[i] / static_cast<float>(kCost[i]);
        min_eff = std::min(min_eff, eff[i]);
        max_eff = std::max(max_eff, eff[i]);
      }
    }
    const float eff_range = (max_eff > min_eff) ? (max_eff - min_eff) : 1.0f;

    // Dynamic column widths
    int max_cnt = 1, max_upg = 0;
    for (int i = 0; i < kAINumTowerTypes; ++i) {
      max_cnt = std::max(max_cnt, stats_.tower_counts[static_cast<std::size_t>(i)].load());
      max_upg = std::max(max_upg, stats_.upgrade_counts[static_cast<std::size_t>(i)].load());
    }
    const int cnt_w     = std::max(3, static_cast<int>(std::to_string(max_cnt).size()));
    const int upg_w     = static_cast<int>(std::to_string(std::max(1, max_upg)).size());
    const int upg_col_w = 2 + upg_w; // " ★" + digits
    // DPS column: fmt(max_dps, 1) + "/s" gives the widest possible string
    const int dps_w = std::max(4, static_cast<int>(fmt(max_dps, 1).size()));

    // Column header
    lines.push_back(hbox({
      text(rpad("", 13)),
      text(lpad("cnt", cnt_w))                | color(Color::GrayDark),
      text(lpad("upg", upg_col_w))            | color(Color::GrayDark),
      text(" " + lpad("dps", dps_w))           | color(Color::GrayDark),
    }));

    for (int i = 0; i < kAINumTowerTypes; ++i) {
      const std::size_t idx = static_cast<std::size_t>(i);
      const int   cnt = stats_.tower_counts[idx].load();
      const int   upg = stats_.upgrade_counts[idx].load();
      const float dps = dps_val[i];
      const bool  active = cnt > 0;
      const Color tc = active ? kInfo[i].col : Color::GrayDark;

      // Name
      auto name_el = text(rpad(" " + std::string(kInfo[i].name), 13)) | color(tc);
      // Count
      auto cnt_el = text(lpad(std::to_string(cnt), cnt_w))
                  | color(active ? Color::White : Color::GrayDark) | bold;
      // Upgrades: fixed display width " ★<upg_w digits>" or matching spaces
      const std::string upg_str = upg > 0
          ? " \xe2\x98\x85" + lpad(std::to_string(upg), upg_w)
          : std::string(static_cast<std::size_t>(upg_col_w), ' ');
      auto upg_el = text(upg_str) | color(Color::Yellow1);

      // DPS: "--" only if tower has never been placed.
      // Once placed, show actual value (0.0 if idle this batch).
      // Color-grade by efficiency (DPS/cost) red→yellow→green.
      Color dps_color = Color::GrayDark;
      std::string dps_str = "--";
      if (dps > 0.0f) {
        dps_str = fmt(dps, 1);
        // Interpolate hue 0°(red)→60°(yellow)→120°(green) based on efficiency rank
        const float t = (max_eff > min_eff)
            ? std::clamp((eff[i] - min_eff) / eff_range, 0.0f, 1.0f)
            : 1.0f; // only one active tower → call it best (green)
        uint8_t r, g;
        if (t <= 0.5f) {
          r = 255;
          g = static_cast<uint8_t>(t * 2.0f * 255.0f);
        } else {
          r = static_cast<uint8_t>((1.0f - t) * 2.0f * 255.0f);
          g = 255;
        }
        dps_color = Color(r, g, static_cast<uint8_t>(0));
      } else if (active) {
        // Previously placed but not used in the last batch — show 0.0 in neutral gray
        dps_str = fmt(0.0f, 1);
        dps_color = Color::GrayLight;
      }
      auto dps_el = text(" " + lpad(dps_str, dps_w)) | color(dps_color);

      lines.push_back(hbox({name_el, cnt_el, upg_el, dps_el}));
    }

    // ── Output bias section ─────────────────────────────────────────────────
    std::array<float, kAINumTowerTypes> pref{};
    float upg_bias = 0.0f;
    {
      std::lock_guard<std::mutex> lock(player_mutex_);
      const auto& net = display_player_.Net();
      const int off_b3 = net.n_in * net.n_h1
                       + net.n_h1
                       + net.n_h1 * net.n_h2
                       + net.n_h2
                       + net.n_h2 * net.n_out;
      const float* b3 = net.params.data() + off_b3;
      for (int t = 0; t < kAINumTowerTypes; ++t) {
        float s = 0.0f;
        for (int j = 0; j < kAINumCandidates; ++j)
          s += b3[kAIActPlace + t * kAINumCandidates + j];
        pref[static_cast<std::size_t>(t)] = s / static_cast<float>(kAINumCandidates);
      }
      float us = 0.0f;
      for (int j = 0; j < kAINumCandidates; ++j)
        us += b3[kAIActUpgrade + j];
      upg_bias = us / static_cast<float>(kAINumCandidates);
    }

    float b_lo = upg_bias, b_hi = upg_bias;
    for (float v : pref) { b_lo = std::min(b_lo, v); b_hi = std::max(b_hi, v); }
    const float b_range = std::max(b_hi - b_lo, 0.001f);

    constexpr int kBiasBarW = 8;
    constexpr int kBiasLblW = 13;

    auto fmt_bias = [](float v) -> std::string {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%+.2f", static_cast<double>(v));
      return buf;
    };

    auto bias_row = [&](const std::string& name, float v, Color col) {
      const int filled = static_cast<int>((v - b_lo) / b_range * kBiasBarW + 0.5f);
      std::string bar, empty;
      for (int k = 0; k < filled;        ++k) bar   += "█";
      for (int k = filled; k < kBiasBarW; ++k) empty += " ";
      std::string lbl = name;
      lbl.resize(static_cast<std::size_t>(kBiasLblW), ' ');
      return hbox({
        text(" " + lbl) | color(Color::GrayLight),
        text(bar)       | color(col),
        text(empty + " "),
        text(fmt_bias(v)) | color(Color::GrayDark),
      });
    };

    lines.push_back(ftxui::separator());
    lines.push_back(text(" output bias") | ftxui::bold | color(Color::White));
    for (int t = 0; t < kAINumTowerTypes; ++t)
      lines.push_back(bias_row(kInfo[static_cast<std::size_t>(t)].name,
                               pref[static_cast<std::size_t>(t)],
                               kInfo[static_cast<std::size_t>(t)].col));
    lines.push_back(ftxui::separator());
    lines.push_back(bias_row("Upgrade", upg_bias, Color::White));

    return vbox(std::move(lines)) | ftxui::border;
  }

  // ── Members ──────────────────────────────────────────────────────────────
  ftxui::ScreenInteractive& screen_;
  AIStats                   stats_;

  // Best player published by the training thread; read by the display tick.
  mutable std::mutex player_mutex_;
  AIPlayer           display_player_{42};

  bool fast_display_ = false;
  int  quit_presses_ = 0;

  std::unique_ptr<GameAIBridge>          display_game_;
  std::array<int, kAINumTowerTypes>      prev_display_counts_{};
  std::array<int, kAINumTowerTypes>      prev_display_upgrades_{};
  ftxui::Element                display_game_render_ = text("");
  int  display_ticks_since_decision_ = 0;
  int  display_game_over_ticks_ = 0; // countdown before resetting after game over

  std::atomic<bool> running_{true};
  std::atomic<bool> tick_pending_{false};
  std::thread       training_thread_;
  std::thread       ticker_;
};

} // namespace

ftxui::Component MakeAIComponent(ftxui::ScreenInteractive& screen,
                                 const std::string& weights_path,
                                 bool fast_forward,
                                 bool fresh) {
  return std::make_shared<AIGameComponent>(screen, weights_path, fast_forward, fresh);
}
