#pragma once

#include "game/ai_interface.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>

ftxui::Component MakeGameComponent(ftxui::ScreenInteractive &screen,
                                   bool dev_mode = false);

// ── AI bridge ───────────────────────────────────────────────────────────────
// Opaque handle to a headless game instance for AI training.
// The full Game class stays inside game.cpp's anonymous namespace.
class GameAIBridge {
public:
  explicit GameAIBridge(bool headless = true);
  ~GameAIBridge();
  GameAIBridge(const GameAIBridge &) = delete;
  GameAIBridge &operator=(const GameAIBridge &) = delete;

  void Reset(); // start a new episode
  void Tick();  // advance one game frame (~16 ms)
  bool
  Act(AICommand cmd); // execute action; returns false if action was invalid
  AIObservation Observe() const;
  bool IsTerminal() const;
  AIEpisodeResult GetResult() const;
  ftxui::Element Render() const;
  int Wave() const;
  int MapIndex() const;

  void ToggleSfx();
  void ToggleMusic();

  // Number of game ticks between AI decisions (~0.2 s of game time at 60 fps)
  static constexpr int kDecisionInterval = 12;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
