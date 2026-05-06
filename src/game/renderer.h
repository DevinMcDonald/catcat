#pragma once

#include <ftxui/dom/elements.hpp>

#include "game/game_state.h"
#include "game/map_path.h"
#include "game/types.h"

class Renderer {
public:
  ftxui::Element RenderBoard(const GameState &state, const MapPath &map,
                             const UIState &ui) const;
  ftxui::Element RenderStats(const GameState &state, const UIState &ui) const;
  static ftxui::Element BlankBoard();

private:
  static ftxui::Color EnemyColor(const Enemy &e);
};
