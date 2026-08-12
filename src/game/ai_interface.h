#pragma once
#include <array>

// Dimensions — must stay in sync with game.cpp constants and Tower::Type count.
constexpr int kAINumCandidates = 60;  // top N placement positions per map
constexpr int kAINumTowerTypes = 7;   // Default..Galactic
constexpr int kAINumEnemies = 8;      // top enemies tracked in obs
constexpr int kAICandidateFeats = 18; // features per candidate slot

// Feature vector layout (indices shown for each block):
//  [0..6]              global: kibbles, lives, wave, map_idx, wave_active,
//                        local_wave_frac, spawn_remaining_frac
//  [7..13]             unlocked flag per tower type
//  [14..20]            can-afford-build flag per tower type
//  [21..27]            can-afford-unlock flag per tower type
//  [28..1107]          per candidate (60×18): buildable_1x1, buildable_2x2,
//                        has_tower, type/6, upgraded,
//                        inc_cov_2.5, inc_cov_3.5, inc_cov_5.0,
//                        cooldown_frac, can_afford_upgrade, path_position_frac,
//                        neighbor_type[0..6] (one flag per tower type)
//  [1108..1131]        top 8 enemies (3 each): progress, hp_norm, type_norm
constexpr int kAIObsGlobal = 7;
constexpr int kAIObsTowerInfo = kAINumTowerTypes * 3;                  // 21
constexpr int kAIObsCandidates = kAINumCandidates * kAICandidateFeats; // 810
constexpr int kAIObsEnemies = kAINumEnemies * 3;                       // 24
constexpr int kAIObs =
    kAIObsGlobal + kAIObsTowerInfo + kAIObsCandidates + kAIObsEnemies; // 862

// Action space layout:
//  0              NOOP
//  1              START_WAVE
//  2..8           UNLOCK tower type[0..6]
//  9..218         PLACE type[i] at candidate[j]  (9 + i*30 + j)
//  219..248       UPGRADE at candidate[j]
//  249..278       SELL at candidate[j]
constexpr int kAIActNoop = 0;
constexpr int kAIActStartWave = 1;
constexpr int kAIActUnlock = 2;                              // + type_idx
constexpr int kAIActPlace = kAIActUnlock + kAINumTowerTypes; // + type*30 + pos
constexpr int kAIActUpgrade =
    kAIActPlace + kAINumTowerTypes * kAINumCandidates;       // + pos
constexpr int kAIActSell = kAIActUpgrade + kAINumCandidates; // + pos
constexpr int kAINumActions = kAIActSell + kAINumCandidates; // 279

struct AIObservation {
    std::array<float, kAIObs> features{};
    std::array<bool, kAINumActions> valid{}; // action mask
};

// The AI returns a single integer index into [0, kAINumActions).
// Game::AIAct() decodes and executes it (noop if invalid).
struct AICommand {
    int action = kAIActNoop;
};

// Returned by Game::AIFitness() after a terminal episode.
struct AIEpisodeResult {
    float fitness = 0.0f;
    int waves_cleared = 0;
    bool victory = false;
    // per-type count of towers placed during the episode (for diversity bonus)
    std::array<int, kAINumTowerTypes> tower_type_counts{};
    // per-type count of upgrades applied (subset of tower_type_counts; never
    // decrements)
    std::array<int, kAINumTowerTypes> tower_upgrade_counts{};
    // per-type accumulated (damage × shots) — proxy for damage contribution
    std::array<int, kAINumTowerTypes> tower_damage_dealt{};
};
