#pragma once

#include <vector>

#include "game/types.h"

class MapPath {
public:
  void Init();
  void SetMap(int map_index);
  const MapDef &GetMap(int map_index) const;
  bool OccupiesPath(const Position &p, int size) const;
  Position EnemyCell(const Enemy &e) const;
  const std::vector<Position> &Path() const { return path_; }
  const std::vector<std::vector<bool>> &PathMask() const { return path_mask_; }
  int MapCount() const { return static_cast<int>(maps_.size()); }

private:
  std::vector<MapDef> maps_;
  std::vector<Position> path_;
  std::vector<std::vector<bool>> path_mask_;
};
