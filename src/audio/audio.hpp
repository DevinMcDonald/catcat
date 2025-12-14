#pragma once

#include <string>
#include <vector>

class AudioSystem {
 public:
  AudioSystem();
  ~AudioSystem();

  bool Init(const std::string& config_path);
  void ReloadConfig();
  void PlayEvent(const std::string& name);
  void SetMusicForMap(int map_index);
  void Update();
  void ToggleSfx();
  void ToggleMusic();
  bool SfxEnabled() const;
  bool MusicEnabled() const;

 private:
  class Impl;
  Impl* impl_;
};
