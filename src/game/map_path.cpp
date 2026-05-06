#include "game/map_path.h"

#include <algorithm>

#include <ftxui/screen/color.hpp>

void MapPath::Init() {
  maps_.clear();
  maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                          {12, kBoardHeight / 2},
                          {12, 4},
                          {30, 4},
                          {30, kBoardHeight - 5},
                          {kBoardWidth - 1, kBoardHeight - 5}},
                         1,
                         ftxui::Color::DarkGreen,
                         ftxui::Color::DarkGoldenrod});
  maps_.push_back(MapDef{{{0, 3},
                          {10, 3},
                          {10, 12},
                          {25, 12},
                          {25, kBoardHeight - 6},
                          {kBoardWidth - 1, kBoardHeight - 6}},
                         2,
                         ftxui::Color::DarkSlateGray3,
                         ftxui::Color::DarkTurquoise});
  maps_.push_back(MapDef{{{0, kBoardHeight - 4},
                          {15, kBoardHeight - 4},
                          {15, 6},
                          {32, 6},
                          {32, kBoardHeight / 2},
                          {kBoardWidth - 1, kBoardHeight / 2}},
                         1,
                         ftxui::Color::DarkOliveGreen3,
                         ftxui::Color::Gold3});
  maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                          {8, kBoardHeight / 2},
                          {8, 6},
                          {20, 6},
                          {20, kBoardHeight - 8},
                          {35, kBoardHeight - 8},
                          {35, 5},
                          {kBoardWidth - 1, 5}},
                         3,
                         ftxui::Color::DarkBlue,
                         ftxui::Color::CornflowerBlue});
  maps_.push_back(MapDef{{{0, 8},
                          {14, 8},
                          {14, kBoardHeight - 6},
                          {28, kBoardHeight - 6},
                          {28, 6},
                          {kBoardWidth - 1, 6}},
                         2,
                         ftxui::Color::DarkKhaki,
                         ftxui::Color::DarkOrange});
  maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                          {10, kBoardHeight / 2},
                          {10, 3},
                          {20, 3},
                          {20, kBoardHeight - 4},
                          {40, kBoardHeight - 4},
                          {40, 8},
                          {kBoardWidth - 1, 8}},
                         2,
                         ftxui::Color::DarkSlateGray1,
                         ftxui::Color::LightSkyBlue1});
  maps_.push_back(MapDef{{{0, kBoardHeight - 5},
                          {18, kBoardHeight - 5},
                          {18, 5},
                          {kBoardWidth - 2, 5},
                          {kBoardWidth - 2, kBoardHeight / 2}},
                         1,
                         ftxui::Color::DarkOliveGreen3,
                         ftxui::Color::GreenYellow});
  maps_.push_back(MapDef{{{0, 4},
                          {8, 4},
                          {8, kBoardHeight - 4},
                          {24, kBoardHeight - 4},
                          {24, 4},
                          {kBoardWidth - 1, 4}},
                         2,
                         ftxui::Color::DarkMagenta,
                         ftxui::Color::DeepPink3});
  maps_.push_back(MapDef{{{0, kBoardHeight / 2},
                          {12, kBoardHeight / 2},
                          {12, 6},
                          {22, 6},
                          {22, kBoardHeight - 7},
                          {34, kBoardHeight - 7},
                          {34, 5},
                          {kBoardWidth - 1, 5}},
                         2,
                         ftxui::Color::DarkSeaGreen3,
                         ftxui::Color::Chartreuse1});
  maps_.push_back(MapDef{{{0, 2},
                          {16, 2},
                          {16, kBoardHeight - 3},
                          {30, kBoardHeight - 3},
                          {30, 7},
                          {kBoardWidth - 1, 7}},
                         3,
                         ftxui::Color::DarkRed,
                         ftxui::Color::OrangeRed1});
}

void MapPath::SetMap(int map_index) {
  const MapDef &map = GetMap(map_index);
  path_.clear();
  for (size_t i = 1; i < map.anchors.size(); ++i) {
    const auto &from = map.anchors[i - 1];
    const auto &to = map.anchors[i];
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
  for (const auto &p : path_) {
    for (int dy = -map.path_width + 1; dy <= map.path_width - 1; ++dy) {
      for (int dx = -map.path_width + 1; dx <= map.path_width - 1; ++dx) {
        const int cx = p.x + dx;
        const int cy = p.y + dy;
        if (cy >= 0 && cy < kBoardHeight && cx >= 0 && cx < kBoardWidth) {
          path_mask_[static_cast<size_t>(cy)][static_cast<size_t>(cx)] = true;
        }
      }
    }
  }
}

const MapDef &MapPath::GetMap(int map_index) const {
  return maps_[static_cast<size_t>(map_index)];
}

bool MapPath::OccupiesPath(const Position &p, int size) const {
  for (int dy = 0; dy < size; ++dy) {
    for (int dx = 0; dx < size; ++dx) {
      const int cx = p.x + dx;
      const int cy = p.y + dy;
      if (cx < 0 || cy < 0 || cx >= kBoardWidth || cy >= kBoardHeight) {
        return true;
      }
      if (path_mask_[static_cast<size_t>(cy)][static_cast<size_t>(cx)]) {
        return true;
      }
    }
  }
  return false;
}

Position MapPath::EnemyCell(const Enemy &e) const {
  const int idx =
      static_cast<int>(std::clamp(std::floor(e.path_progress), 0.0F,
                                  static_cast<float>(path_.size() - 1)));
  const size_t i = static_cast<size_t>(idx);
  Position base = path_[i];
  int dx = 0;
  int dy = 0;
  if (i + 1 < path_.size()) {
    dx = path_[i + 1].x - base.x;
    dy = path_[i + 1].y - base.y;
  } else if (i > 0) {
    dx = base.x - path_[i - 1].x;
    dy = base.y - path_[i - 1].y;
  }
  dx = (dx > 0) - (dx < 0);
  dy = (dy > 0) - (dy < 0);
  Position perp{-dy, dx};
  base.x = std::clamp(base.x + perp.x * e.lane_offset, 0, kBoardWidth - 1);
  base.y = std::clamp(base.y + perp.y * e.lane_offset, 0, kBoardHeight - 1);
  return base;
}
