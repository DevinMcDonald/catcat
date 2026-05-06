#include "game/renderer.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

using ftxui::bgcolor;
using ftxui::bold;
using ftxui::color;
using ftxui::hbox;
using ftxui::inverted;
using ftxui::separator;
using ftxui::text;
using ftxui::vbox;

ftxui::Color Renderer::EnemyColor(const Enemy &e) {
  const float ratio =
      static_cast<float>(e.hp) / static_cast<float>(std::max(1, e.max_hp));
  if (ratio > 0.75F)
    return ftxui::Color::RedLight;
  if (ratio > 0.5F)
    return ftxui::Color::Orange1;
  if (ratio > 0.25F)
    return ftxui::Color::Yellow1;
  return ftxui::Color::GrayLight;
}

ftxui::Element Renderer::BlankBoard() {
  std::vector<ftxui::Element> rows;
  rows.reserve(kBoardHeight);
  const std::string empty_row(static_cast<size_t>(kBoardWidth) * 2, ' ');
  for (int y = 0; y < kBoardHeight; ++y) {
    rows.push_back(text(empty_row) | bgcolor(ftxui::Color::Black) |
                   color(ftxui::Color::Black));
  }
  return vbox(std::move(rows));
}

ftxui::Element Renderer::RenderBoard(const GameState &state,
                                     const MapPath &map,
                                     const UIState &ui) const {
  std::vector<std::vector<char>> glyphs(kBoardHeight,
                                        std::vector<char>(kBoardWidth, ' '));
  const auto &mapdef = map.GetMap(state.map_index);
  std::vector<std::vector<ftxui::Color>> backgrounds(
      kBoardHeight,
      std::vector<ftxui::Color>(kBoardWidth, mapdef.background));
  std::vector<std::vector<ftxui::Color>> foregrounds(
      kBoardHeight,
      std::vector<ftxui::Color>(kBoardWidth, ftxui::Color::White));
  std::vector<std::vector<bool>> highlight(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));
  std::vector<std::vector<bool>> enemy_mask(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));

  const bool show_overlay = ui.overlay_enabled || state.held_tower.has_value();
  std::vector<std::vector<bool>> range_hint_base(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));
  std::vector<std::vector<bool>> range_hint_preview(
      kBoardHeight, std::vector<bool>(kBoardWidth, false));
  auto display_range = [&](const Tower &t) {
    if (t.type == Tower::Type::Kitty && t.upgraded) {
      return t.range + kKittyJumpBonusRange;
    }
    return t.range;
  };
  if (show_overlay) {
    for (const auto &t : state.towers) {
      const auto def = GetDef(t.type);
      if (!def.show_range) {
        continue;
      }
      const auto center = TowerCenter(t);
      if (t.type == Tower::Type::Kitty && !t.upgraded) {
        const auto cells = KittyOverlayCells(center);
        for (const auto &cell : cells) {
          if (cell.x < 0 || cell.y < 0 || cell.x >= kBoardWidth ||
              cell.y >= kBoardHeight) {
            continue;
          }
          range_hint_base[static_cast<size_t>(cell.y)]
                         [static_cast<size_t>(cell.x)] = true;
        }
        continue;
      }
      for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
          Position cell{x, y};
          if (InRange(center, cell, display_range(t))) {
            range_hint_base[static_cast<size_t>(y)][static_cast<size_t>(x)] =
                true;
          }
        }
      }
    }
    const TowerDef preview_def = GetDef(ui.selected_type);
    if (preview_def.show_range) {
      const Vec2 center{
          static_cast<float>(ui.cursor.x) +
              (static_cast<float>(preview_def.size) - 1.0F) / 2.0F,
          static_cast<float>(ui.cursor.y) +
              (static_cast<float>(preview_def.size) - 1.0F) / 2.0F};
      if (preview_def.type == Tower::Type::Kitty) {
        const auto cells = KittyOverlayCells(center);
        for (const auto &cell : cells) {
          if (cell.x < 0 || cell.y < 0 || cell.x >= kBoardWidth ||
              cell.y >= kBoardHeight) {
            continue;
          }
          range_hint_preview[static_cast<size_t>(cell.y)]
                            [static_cast<size_t>(cell.x)] = true;
        }
      } else {
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
    }
  }

  for (int y = 0; y < kBoardHeight; ++y) {
    const auto yi = static_cast<size_t>(y);
    for (int x = 0; x < kBoardWidth; ++x) {
      const auto xi = static_cast<size_t>(x);
      if (map.PathMask()[yi][xi]) {
        backgrounds[yi][xi] = mapdef.path_color;
        glyphs[yi][xi] = '.';
        foregrounds[yi][xi] = ftxui::Color::Black;
      }
    }
  }

  for (const auto &t : state.towers) {
    const char glyph =
        t.type == Tower::Type::Thunder     ? (t.upgraded ? 'T' : 't')
        : t.type == Tower::Type::Fat       ? (t.upgraded ? 'F' : 'f')
        : t.type == Tower::Type::Kitty     ? (t.upgraded ? 'K' : 'k')
        : t.type == Tower::Type::Catatonic ? (t.upgraded ? 'C' : 'c')
        : t.type == Tower::Type::Galactic  ? (t.upgraded ? 'G' : 'g')
                                           : (t.upgraded ? 'D' : 'd');
    const ftxui::Color bg =
        t.type == Tower::Type::Thunder     ? ftxui::Color::Blue1
        : t.type == Tower::Type::Fat       ? ftxui::Color::DarkOliveGreen3
        : t.type == Tower::Type::Kitty     ? ftxui::Color::Pink1
        : t.type == Tower::Type::Catatonic ? ftxui::Color::Purple
        : t.type == Tower::Type::Galactic  ? ftxui::Color::LightSteelBlue
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

  for (const auto &e : state.enemies) {
    const auto pos = map.EnemyCell(e);
    const auto yi = static_cast<size_t>(pos.y);
    const auto xi = static_cast<size_t>(pos.x);
    char g = 'r';
    ftxui::Color fg = EnemyColor(e);
    std::optional<ftxui::Color> bg_override;
    switch (e.type) {
    case EnemyType::Mouse:
      g = 'm';
      fg = ftxui::Color::Grey70;
      bg_override = std::nullopt;
      break;
    case EnemyType::Rat:
      g = 'r';
      fg = EnemyColor(e);
      bg_override = ftxui::Color::Grey23;
      break;
    case EnemyType::BigRat:
      g = 'R';
      fg = ftxui::Color::RedLight;
      bg_override = ftxui::Color::Grey35;
      break;
    case EnemyType::Dog:
      g = 'D';
      fg = ftxui::Color::White;
      bg_override = ftxui::Color::DarkRed;
      break;
    }
    glyphs[yi][xi] = g;
    if (bg_override.has_value())
      backgrounds[yi][xi] = *bg_override;
    foregrounds[yi][xi] = fg;
    enemy_mask[yi][xi] = true;
  }

  for (const auto &p : state.projectiles) {
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

  for (const auto &b : state.beams) {
    for (const auto &cell : b.cells) {
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

  for (const auto &ah : state.area_highlights) {
    for (const auto &cell : ah.cells) {
      if (cell.y < 0 || cell.y >= kBoardHeight || cell.x < 0 ||
          cell.x >= kBoardWidth) {
        continue;
      }
      const auto yi = static_cast<size_t>(cell.y);
      const auto xi = static_cast<size_t>(cell.x);
      glyphs[yi][xi] = ah.glyph;
      foregrounds[yi][xi] = ah.color;
      backgrounds[yi][xi] =
          BlendColor(backgrounds[yi][xi], ah.color, 0.08F);
    }
  }

  for (const auto &sw : state.shockwaves) {
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

  for (const auto &hs : state.hit_splats) {
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

  const auto apply_tint = [&](size_t yi, size_t xi,
                               const ftxui::Color &tint, float alpha) {
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

  if (show_overlay) {
    const TowerDef preview_def_place = GetDef(
        state.held_tower.has_value() ? state.held_tower->tower.type
                                     : ui.selected_type);
    const bool preview_is_kitty =
        preview_def_place.type == Tower::Type::Kitty;
    const bool can_place_preview =
        ui.cursor.x >= 0 && ui.cursor.y >= 0 &&
        ui.cursor.x + preview_def_place.size - 1 < kBoardWidth &&
        ui.cursor.y + preview_def_place.size - 1 < kBoardHeight &&
        !map.OccupiesPath(ui.cursor, preview_def_place.size) &&
        !OverlapsTower(state, ui.cursor, preview_def_place.size);
    if (preview_is_kitty) {
      const Vec2 center{
          static_cast<float>(ui.cursor.x) +
              (static_cast<float>(preview_def_place.size) - 1.0F) / 2.0F,
          static_cast<float>(ui.cursor.y) +
              (static_cast<float>(preview_def_place.size) - 1.0F) / 2.0F};
      const auto cells = KittyOverlayCells(center);
      for (const auto &cell : cells) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= kBoardWidth ||
            cell.y >= kBoardHeight) {
          continue;
        }
        range_hint_preview[static_cast<size_t>(cell.y)]
                          [static_cast<size_t>(cell.x)] = true;
      }
    }
    for (int dy = 0; dy < preview_def_place.size; ++dy) {
      for (int dx = 0; dx < preview_def_place.size; ++dx) {
        const int gx = ui.cursor.x + dx;
        const int gy = ui.cursor.y + dy;
        if (gx < 0 || gy < 0 || gx >= kBoardWidth || gy >= kBoardHeight) {
          continue;
        }
        const auto yi = static_cast<size_t>(gy);
        const auto xi = static_cast<size_t>(gx);
        glyphs[yi][xi] = can_place_preview ? '+' : 'X';
        foregrounds[yi][xi] =
            can_place_preview ? ftxui::Color(ftxui::Color::LightSkyBlue1)
                              : ftxui::Color(ftxui::Color::RedLight);
      }
    }
  }

  if (state.game_over) {
    for (int y = 0; y < kBoardHeight; ++y) {
      const auto yi = static_cast<size_t>(y);
      for (int x = 0; x < kBoardWidth; ++x) {
        const auto xi = static_cast<size_t>(x);
        backgrounds[yi][xi] = ftxui::Color::Grey23;
        foregrounds[yi][xi] = ftxui::Color::Grey70;
      }
    }
  }

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
      if (ui.cursor.x == x && ui.cursor.y == y) {
        cell = cell | inverted;
      }
      cells.push_back(std::move(cell));
    }
    rows.push_back(hbox(std::move(cells)));
  }

  auto board_elem = vbox(std::move(rows));
  if (state.game_over) {
    board_elem = board_elem | bgcolor(ftxui::Color::Black) |
                 color(ftxui::Color::Grey70) | bold;
  }
  return board_elem;
}

ftxui::Element Renderer::RenderStats(const GameState &state,
                                     const UIState &ui) const {
  std::string wave_text =
      state.wave_active ? "Wave " + std::to_string(state.wave) : "Waiting";
  if (state.auto_waves) {
    wave_text += " (auto)";
  }
  std::vector<ftxui::Element> lines;
  lines.push_back(text("cat cat"));
  if (ui.dev_mode) {
    lines.push_back(text("DEV MODE"));
  }
  lines.push_back(text("Status: " + wave_text));
  lines.push_back(
      text("Map: " + std::to_string(state.map_index + 1) + "/10"));
  lines.push_back(text("Speed: " + std::string(state.fast_forward
                                                    ? "FAST x5 (f)"
                                                    : "Normal (f)")));
  lines.push_back(text("Lives: " + std::to_string(state.lives)));
  lines.push_back(text("Kibbles: " + std::to_string(state.kibbles)));
  lines.push_back(text("Cats: " + std::to_string(state.towers.size())));
  lines.push_back(separator());

  const auto selected_def = GetDef(ui.selected_type);
  lines.push_back(text("Selected: " + selected_def.name));
  lines.push_back(separator());

  const auto defs = SortedDefs();

  if (ui.view_shop) {
    lines.push_back(
        text("shop (press 1-6 to buy/select, p to return)"));
    std::vector<TowerDef> locked;
    for (const auto &d : defs) {
      if (!IsUnlocked(state, d.type)) {
        locked.push_back(d);
      }
    }
    if (locked.empty()) {
      lines.push_back(text("All cats unlocked!"));
    } else {
      size_t name_w = 0;
      size_t cost_w = 0;
      std::vector<std::string> cost_cols(locked.size());
      size_t num_w = 0;
      for (size_t i = 0; i < locked.size(); ++i) {
        const auto &d = locked[i];
        name_w = std::max(name_w, d.name.size() + 3);
        const std::string num = std::to_string(d.cost * 10);
        num_w = std::max(num_w, num.size());
      }
      for (size_t i = 0; i < locked.size(); ++i) {
        const auto &d = locked[i];
        const std::string num = std::to_string(d.cost * 10);
        const std::string spaced_num =
            num +
            std::string(num_w > num.size() ? num_w - num.size() : 0, ' ');
        cost_cols[i] = spaced_num + " kib";
        cost_w = std::max(cost_w, cost_cols[i].size());
      }
      for (size_t i = 0; i < locked.size(); ++i) {
        const auto &d = locked[i];
        std::string desc;
        if (d.type == Tower::Type::Thunder) {
          desc = "Laser, dmg " + std::to_string(d.damage) + ", slow fire";
        } else if (d.type == Tower::Type::Fat) {
          desc = "2x2 AOE, dmg " + std::to_string(d.damage);
        } else if (d.type == Tower::Type::Kitty) {
          desc = "Swipe 4x6, dmg " + std::to_string(d.damage) +
                 " (jumps when upgraded)";
        } else if (d.type == Tower::Type::Catatonic) {
          desc = "Sleep pulse, slows";
        } else if (d.type == Tower::Type::Galactic) {
          desc = "Cosmic cone blast";
        } else {
          desc = "dmg " + std::to_string(d.damage);
        }
        const std::string key = std::to_string(TypeKey(d.type)) + ") ";
        const std::string line = PadRight(key + d.name, name_w + 2) +
                                 PadRight(cost_cols[i], cost_w + 2) + desc;
        lines.push_back(text(line));
      }
    }
  } else {
    lines.push_back(text("press p to view shop"));
    lines.push_back(text("unlocked cats:"));
    std::vector<TowerDef> unlocked_defs;
    for (const auto &d : defs) {
      if (!IsUnlocked(state, d.type)) {
        continue;
      }
      unlocked_defs.push_back(d);
    }
    if (!unlocked_defs.empty()) {
      size_t name_w = 0;
      size_t cost_w = 0;
      std::vector<std::string> cost_cols(unlocked_defs.size());
      size_t num_w = 0;
      for (size_t i = 0; i < unlocked_defs.size(); ++i) {
        const auto &d = unlocked_defs[i];
        const std::string key = std::to_string(TypeKey(d.type)) + ") ";
        name_w = std::max(name_w, key.size() + d.name.size());
        const std::string num = std::to_string(d.cost);
        num_w = std::max(num_w, num.size());
      }
      for (size_t i = 0; i < unlocked_defs.size(); ++i) {
        const auto &d = unlocked_defs[i];
        const std::string num = std::to_string(d.cost);
        const std::string spaced_num =
            num +
            std::string(num_w > num.size() ? num_w - num.size() : 0, ' ');
        cost_cols[i] = spaced_num + " kib";
        cost_w = std::max(cost_w, cost_cols[i].size());
      }
      for (size_t i = 0; i < unlocked_defs.size(); ++i) {
        const auto &d = unlocked_defs[i];
        const std::string key = std::to_string(TypeKey(d.type)) + ") ";
        const std::string line = PadRight(key + d.name, name_w + 2) +
                                 PadRight(cost_cols[i], cost_w + 2);
        lines.push_back(text(line));
      }
    }
  }

  if (state.game_over) {
    lines.push_back(text("Game Over") | bold |
                    color(ftxui::Color::RedLight));
  }

  if (!ui.warning_text.empty() && ui.warning_timer > 0.0F) {
    lines.push_back(separator());
    std::stringstream ss(ui.warning_text);
    std::string line;
    while (std::getline(ss, line)) {
      lines.push_back(text(line) | color(ftxui::Color::YellowLight));
    }
  }

  if (ui.show_controls) {
    lines.push_back(separator());
    lines.push_back(text("controls (press h to hide):"));
    lines.push_back(text("arrows/WASD - move cursor"));
    lines.push_back(text("space/c     - place selected cat"));
    lines.push_back(text("m           - pick up tower under cursor"));
    lines.push_back(text("u           - upgrade tower (cost 5x)"));
    lines.push_back(text("x           - sell tower (60% refund)"));
    lines.push_back(text("esc         - toggle overlay / cancel move"));
    lines.push_back(text("1-6         - select cat type (by cost)"));
    lines.push_back(text("p           - toggle shop view"));
    lines.push_back(text("n/N         - next wave / auto waves"));
    lines.push_back(text("f           - toggle fast forward x5"));
    lines.push_back(text("t           - toggle sfx"));
    lines.push_back(text("y           - toggle music"));
    if (ui.dev_mode) {
      lines.push_back(text(">           - skip to next map (dev)"));
    }
    lines.push_back(text("q q q       - quit"));
  } else {
    lines.push_back(separator());
    lines.push_back(text("press h for controls"));
  }

  lines.push_back(separator());

  return vbox(std::move(lines));
}
