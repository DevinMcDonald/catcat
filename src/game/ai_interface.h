#pragma once
#include <array>

// Dimensions — must stay in sync with game.cpp constants and Tower::Type count.
constexpr int kAINumCandidates = 30;  // top N placement positions per map
constexpr int kAINumTowerTypes = 7;   // Default..Galactic
constexpr int kAINumEnemies    = 8;   // top enemies tracked in obs

// Feature vector layout (indices shown for each block):
//  [0..4]              global: kibbles, lives, wave, map_idx, wave_active
//  [5..11]             unlocked flag per tower type
//  [12..18]            can-afford-build flag per tower type
//  [19..25]            can-afford-unlock flag per tower type
//  [26..205]           per candidate (30×6): buildable, has_tower,
//                        type/6, upgraded, coverage, cooldown_frac
//  [206..229]          top 8 enemies (3 each): progress, hp_norm, type_norm
constexpr int kAIObsGlobal    = 5;
constexpr int kAIObsTowerInfo = kAINumTowerTypes * 3;   // 21
constexpr int kAIObsCandidates= kAINumCandidates * 6;  // 180
constexpr int kAIObsEnemies   = kAINumEnemies * 3;     // 24
constexpr int kAIObs = kAIObsGlobal + kAIObsTowerInfo + kAIObsCandidates + kAIObsEnemies; // 230

// Action space layout:
//  0              NOOP
//  1              START_WAVE
//  2..8           UNLOCK tower type[0..6]
//  9..218         PLACE type[i] at candidate[j]  (9 + i*30 + j)
//  219..248       UPGRADE at candidate[j]
//  249..278       SELL at candidate[j]
constexpr int kAIActNoop      = 0;
constexpr int kAIActStartWave = 1;
constexpr int kAIActUnlock    = 2;                                          // + type_idx
constexpr int kAIActPlace     = kAIActUnlock + kAINumTowerTypes;            // + type*30 + pos
constexpr int kAIActUpgrade   = kAIActPlace  + kAINumTowerTypes * kAINumCandidates; // + pos
constexpr int kAIActSell      = kAIActUpgrade + kAINumCandidates;           // + pos
constexpr int kAINumActions   = kAIActSell    + kAINumCandidates;           // 279

struct AIObservation {
    std::array<float, kAIObs>        features{};
    std::array<bool,  kAINumActions> valid{};  // action mask
};

// The AI returns a single integer index into [0, kAINumActions).
// Game::AIAct() decodes and executes it (noop if invalid).
struct AICommand { int action = kAIActNoop; };

// Returned by Game::AIFitness() after a terminal episode.
struct AIEpisodeResult {
    float fitness       = 0.0f;
    int   waves_cleared = 0;
    bool  victory       = false;
    // per-type count of towers placed during the episode (for diversity bonus)
    std::array<int, kAINumTowerTypes> tower_type_counts{};
    // per-type count of upgrades applied (subset of tower_type_counts; never decrements)
    std::array<int, kAINumTowerTypes> tower_upgrade_counts{};
    // per-type accumulated (damage × shots) — proxy for damage contribution
    std::array<int, kAINumTowerTypes> tower_damage_dealt{};
};
