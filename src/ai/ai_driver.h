#pragma once
#include "game/ai_interface.h"
#include "game/game.h"   // for GameAIBridge

#include <array>
#include <atomic>
#include <chrono>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <random>
#include <string>
#include <vector>

// ── Structured network: global-context MLP + shared per-candidate MLP ────────
//
// Parameter layout (all contiguous in params[]):
//   global MLP : kGIn→kGH→kGEmb    (processes global+tower_info+enemy features)
//   candidate MLP: (kCIn+kGEmb)→kCH→kCEmb  (shared across all 30 candidates)
//   special head : kGEmb→kSpec      (noop / start_wave / unlock×7)
//   place head   : kCEmb→kNTypes    (one score per tower type, per candidate)
//   upgrade head : kCEmb→1
//   sell head    : kCEmb→1
struct CandidateNet {
    static constexpr int kGIn    = kAIObsGlobal + kAIObsTowerInfo + kAIObsEnemies; // 52
    static constexpr int kGH     = 64;
    static constexpr int kGEmb   = 32;
    static constexpr int kCIn    = kAICandidateFeats;  // 18
    static constexpr int kCH     = 48;
    static constexpr int kCEmb   = 32;
    static constexpr int kSpec   = 1 + 1 + kAINumTowerTypes; // noop+start+unlock×7 = 9
    static constexpr int kNTypes = kAINumTowerTypes;

    // Cumulative offsets into params[] — public for display-panel introspection.
    static constexpr int kOffGW1 = 0;
    static constexpr int kOffGB1 = kOffGW1 + kGIn  * kGH;
    static constexpr int kOffGW2 = kOffGB1 + kGH;
    static constexpr int kOffGB2 = kOffGW2 + kGH   * kGEmb;
    static constexpr int kOffCW1 = kOffGB2 + kGEmb;
    static constexpr int kOffCB1 = kOffCW1 + (kCIn + kGEmb) * kCH;
    static constexpr int kOffCW2 = kOffCB1 + kCH;
    static constexpr int kOffCB2 = kOffCW2 + kCH   * kCEmb;
    static constexpr int kOffSPW = kOffCB2 + kCEmb;
    static constexpr int kOffSPB = kOffSPW + kGEmb  * kSpec;
    static constexpr int kOffPLW = kOffSPB + kSpec;
    static constexpr int kOffPLB = kOffPLW + kCEmb  * kNTypes;
    static constexpr int kOffUPW = kOffPLB + kNTypes;
    static constexpr int kOffUPB = kOffUPW + kCEmb;
    static constexpr int kOffSLW = kOffUPB + 1;
    static constexpr int kOffSLB = kOffSLW + kCEmb;
    static constexpr int kTotal  = kOffSLB + 1;  // 3698

    std::vector<float> params;

    CandidateNet();

    std::vector<float> Forward(const std::vector<float>& obs) const;
    CandidateNet       Perturbed(float sigma) const;
    bool               Save(const std::string& path) const;
    bool               Load(const std::string& path);
    std::size_t        NumParams() const { return params.size(); }
};

// ── Training state shared between the training loop and the FTXUI render ────
struct AIStats {
  std::atomic<int>   episodes{0};
  std::atomic<float> best_fitness{-1e9f};
  std::atomic<float> last_fitness{0.0f};
  std::atomic<float> sigma{0.1f};
  std::atomic<int>   wins{0};
  std::atomic<int>   losses{0};
  // Per-type DPS from the most recently completed batch (aggregate damage / total game time)
  std::array<std::atomic<float>, kAINumTowerTypes> last_batch_dps{};
  // tower-type usage counts from the last displayed game
  std::array<std::atomic<int>, kAINumTowerTypes> tower_counts{};
  std::array<std::atomic<int>, kAINumTowerTypes> upgrade_counts{};

  // Start time for episodes/min calculation
  std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};

  // Fitness history ring buffer (written from training thread, read on render)
  static constexpr int kHistLen = 40;
  std::array<std::atomic<float>, kHistLen> history{};
  std::atomic<int> history_head{0};

  void PushHistory(float v) {
    const int h = history_head.load();
    history[static_cast<std::size_t>(h)].store(v);
    history_head.store((h + 1) % kHistLen);
  }

  // Win-rate history ring buffer (per-batch win rate, 0..1)
  std::array<std::atomic<float>, kHistLen> win_rate_history{};
  std::atomic<int> win_rate_head{0};

  void PushWinRate(int batch_wins, int batch_total) {
    const float rate = batch_total > 0
        ? static_cast<float>(batch_wins) / static_cast<float>(batch_total) : 0.0f;
    const int h = win_rate_head.load();
    win_rate_history[static_cast<std::size_t>(h)].store(rate);
    win_rate_head.store((h + 1) % kHistLen);
  }

  // Waves-survived history ring buffer (per-batch average waves cleared)
  std::array<std::atomic<float>, kHistLen> waves_history{};
  std::atomic<int> waves_head{0};
  std::atomic<int> waves_count{0}; // total pushes, capped at kHistLen
  std::atomic<int> best_waves{0};

  void PushWaves(float avg_waves, int best_this_batch) {
    const int h = waves_head.load();
    waves_history[static_cast<std::size_t>(h)].store(avg_waves);
    waves_head.store((h + 1) % kHistLen);
    const int c = waves_count.load();
    if (c < kHistLen) waves_count.store(c + 1);
    if (best_this_batch > best_waves.load()) best_waves.store(best_this_batch);
  }

  // Sigma history ring buffer (fixed scale 0..0.5)
  std::array<std::atomic<float>, kHistLen> sigma_history{};
  std::atomic<int> sigma_head{0};
  std::atomic<int> sigma_count{0};

  void PushSigma(float s) {
    const int h = sigma_head.load();
    sigma_history[static_cast<std::size_t>(h)].store(s);
    sigma_head.store((h + 1) % kHistLen);
    const int c = sigma_count.load();
    if (c < kHistLen) sigma_count.store(c + 1);
  }
};

// ── AI player: encodes state, masks actions, samples from network ────────────
class AIPlayer {
public:
  explicit AIPlayer(int seed = 42);

  bool Load(const std::string& path);
  bool Save(const std::string& path) const;

  AICommand           SelectAction(const AIObservation& obs) const;
  AIPlayer            Perturbed(float sigma) const;
  const CandidateNet& Net() const { return net_; }

private:
  CandidateNet        net_;
  mutable std::mt19937_64 rng_;
};

// ── Trainer: (1+λ)-ES evolutionary strategy ──────────────────────────────────
class Trainer {
public:
  explicit Trainer(const std::string& weights_path, bool fresh = false,
                   int candidates = 8, int eval_games = 10);

  // Run one training batch: candidates evaluated in parallel, each playing
  // eval_games games. Updates internal best weights and stats.
  // Returns the best fitness seen this batch.
  // Pass running=false to abort early (returns 0 without updating stats).
  float RunBatch(AIStats& stats, const std::atomic<bool>& running);

  const AIPlayer& BestPlayer() const { return best_player_; }
  int Candidates() const { return candidates_; }
  int EvalGames()  const { return eval_games_; }

  // Per-candidate evaluation result (used for parallel collection).
  struct EvalResult {
    float fitness = 0.f;
    int wins = 0, losses = 0, waves = 0, ticks = 0;
    std::array<int, kAINumTowerTypes> tower_counts{};
    std::array<int, kAINumTowerTypes> upgrade_counts{};
    std::array<int, kAINumTowerTypes> damage{};
  };

private:
  EvalResult EvaluatePlayer(const AIPlayer& player, const std::atomic<bool>& running) const;

  AIPlayer    best_player_;
  float       best_fitness_;
  float       sigma_;
  int         success_count_;
  int         eval_count_;
  std::string weights_path_;
  int         candidates_;
  int         eval_games_;
};

// ── FTXUI component for --ai mode ────────────────────────────────────────────
ftxui::Component MakeAIComponent(ftxui::ScreenInteractive& screen,
                                 const std::string& weights_path,
                                 bool fast_forward = false,
                                 bool fresh = false,
                                 int candidates = 8,
                                 int eval_games = 10);
