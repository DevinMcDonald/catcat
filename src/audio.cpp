#include "audio.hpp"

#include <memory>
#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#ifdef ENABLE_AUDIO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <nlohmann/json.hpp>
#include <fstream>

struct EventEntry {
  std::vector<std::string> files;
  float volume = 1.0F;
};

struct MusicEntry {
  std::vector<std::string> files;
  float volume = 1.0F;
};

class AudioSystem::Impl {
 public:
  Impl() = default;
  ~Impl() { Shutdown(); }

  bool Init(const std::string& config_path) {
    config_path_ = config_path;
    if (!engine_init_) {
      if (ma_engine_init(nullptr, &engine_) != MA_SUCCESS) {
        return false;
      }
      engine_init_ = true;
    }
    LoadConfig();
    return true;
  }

  void Shutdown() {
    CleanupSounds(true);
    StopMusic();
    if (engine_init_) {
      ma_engine_uninit(&engine_);
      engine_init_ = false;
    }
  }

  void ReloadConfig() { LoadConfig(); }

  void PlayEvent(const std::string& name) {
    if (!engine_init_ || !sfx_enabled_) return;
    auto it = events_.find(name);
    if (it == events_.end() || it->second.files.empty()) return;
    const auto& entry = it->second;
    const auto& list = entry.files;
    const size_t idx = list.size() == 1 ? 0 : static_cast<size_t>(dist_(rng_) % list.size());
    const float vol = std::clamp(entry.volume, 0.0F, 2.0F);
    CleanupSounds(false);

    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file(&engine_, list[idx].c_str(), MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                sound.get()) != MA_SUCCESS) {
      return;
    }
    ma_sound_set_volume(sound.get(), sfx_volume_ * vol);
    ma_sound_start(sound.get());
    active_sounds_.push_back(std::move(sound));
  }

  void SetMusicForMap(int map_index) {
    if (!engine_init_) return;
    StopMusic();
    if (!music_enabled_) return;
    const auto it = music_.find(map_index);
    if (it == music_.end() || it->second.files.empty()) return;
    const auto& tracks = it->second.files;
    const size_t idx = tracks.size() == 1 ? 0 : static_cast<size_t>(dist_(rng_) % tracks.size());
    const std::string& path = tracks[idx];
    if (ma_sound_init_from_file(&engine_, path.c_str(),
                                MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                &music_sound_) != MA_SUCCESS) {
      music_loaded_ = false;
      return;
    }
    current_music_gain_ = std::clamp(it->second.volume, 0.0F, 2.0F);
    ma_sound_set_looping(&music_sound_, MA_TRUE);
    ma_sound_set_volume(&music_sound_, music_volume_ * current_music_gain_);
    ma_sound_start(&music_sound_);
    music_loaded_ = true;
  }

  void ToggleSfx() { sfx_enabled_ = !sfx_enabled_; }
  void ToggleMusic() {
    music_enabled_ = !music_enabled_;
    if (!music_enabled_) {
      StopMusic();
    }
  }

  bool SfxEnabled() const { return sfx_enabled_; }
  bool MusicEnabled() const { return music_enabled_; }

 private:
  std::string ResolvePath(const std::string& base, const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
      return path;  // absolute (unix or windows)
    }
    if (base.empty()) return path;
    if (base.back() == '/') return base + path;
    return base + "/" + path;
  }

  void LoadConfig() {
    events_.clear();
    music_.clear();
    if (config_path_.empty()) return;
    std::ifstream in(config_path_);
    if (!in) return;
    nlohmann::json j;
    in >> j;
    std::string base_dir;
    {
      auto pos = config_path_.find_last_of("/\\");
      if (pos != std::string::npos) {
        base_dir = config_path_.substr(0, pos);
      }
    }
    if (j.contains("volume")) {
      const auto& v = j["volume"];
      if (v.contains("sfx")) sfx_volume_ = std::clamp(v["sfx"].get<float>(), 0.0F, 1.0F);
      if (v.contains("music")) music_volume_ = std::clamp(v["music"].get<float>(), 0.0F, 1.0F);
      ma_engine_set_volume(&engine_, sfx_volume_);
      if (music_loaded_) ma_sound_set_volume(&music_sound_, music_volume_);
    }
    if (j.contains("events")) {
      for (auto it = j["events"].begin(); it != j["events"].end(); ++it) {
        EventEntry entry;
        const auto& v = it.value();
          if (v.is_array()) {
            for (const auto& f : v) {
            entry.files.push_back(ResolvePath(base_dir, f.get<std::string>()));
            }
          } else if (v.is_object()) {
            if (v.contains("files")) {
              for (const auto& f : v["files"]) {
              entry.files.push_back(ResolvePath(base_dir, f.get<std::string>()));
              }
            }
            if (v.contains("volume") && v["volume"].is_number()) {
            entry.volume = std::clamp(v["volume"].get<float>(), 0.0F, 2.0F);
          }
        }
        events_[it.key()] = std::move(entry);
      }
    }
    if (j.contains("music")) {
      for (auto it = j["music"].begin(); it != j["music"].end(); ++it) {
        int map_idx = 0;
        try {
          map_idx = std::stoi(it.key().substr(it.key().find_last_not_of("0123456789") + 1));
        } catch (...) {
          continue;
        }
        MusicEntry entry;
        const auto& v = it.value();
        if (v.is_array()) {
          for (const auto& f : v) {
            entry.files.push_back(f.get<std::string>());
          }
        } else if (v.is_object()) {
          if (v.contains("files")) {
            for (const auto& f : v["files"]) {
              entry.files.push_back(f.get<std::string>());
            }
          }
          if (v.contains("volume") && v["volume"].is_number()) {
            entry.volume = std::clamp(v["volume"].get<float>(), 0.0F, 2.0F);
          }
        }
        music_[map_idx] = std::move(entry);
      }
    }
  }

  void StopMusic() {
    if (music_loaded_) {
      ma_sound_uninit(&music_sound_);
      music_loaded_ = false;
    }
  }

  void CleanupSounds(bool force_all) {
    if (active_sounds_.empty()) return;
    active_sounds_.erase(
        std::remove_if(active_sounds_.begin(), active_sounds_.end(),
                       [&](std::unique_ptr<ma_sound>& s) {
                         if (!s) return true;
                         if (force_all || ma_sound_is_playing(s.get()) == MA_FALSE) {
                           ma_sound_uninit(s.get());
                           return true;
                         }
                         return false;
                       }),
        active_sounds_.end());
  }

  ma_engine engine_{};
  bool engine_init_ = false;
  bool music_loaded_ = false;
  ma_sound music_sound_{};
  std::unordered_map<std::string, EventEntry> events_;
  std::unordered_map<int, MusicEntry> music_;
  std::string config_path_;
  float sfx_volume_ = 1.0F;
  float music_volume_ = 1.0F;
  bool sfx_enabled_ = true;
  bool music_enabled_ = true;
  float current_music_gain_ = 1.0F;
  std::vector<std::unique_ptr<ma_sound>> active_sounds_;
  std::mt19937 rng_{std::random_device{}()};
  std::uniform_int_distribution<int> dist_;
};

AudioSystem::AudioSystem() : impl_(new Impl()) {}
AudioSystem::~AudioSystem() { delete impl_; }
bool AudioSystem::Init(const std::string& config_path) { return impl_->Init(config_path); }
void AudioSystem::ReloadConfig() { impl_->ReloadConfig(); }
void AudioSystem::PlayEvent(const std::string& name) { impl_->PlayEvent(name); }
void AudioSystem::SetMusicForMap(int map_index) { impl_->SetMusicForMap(map_index); }
void AudioSystem::ToggleSfx() { impl_->ToggleSfx(); }
void AudioSystem::ToggleMusic() { impl_->ToggleMusic(); }
bool AudioSystem::SfxEnabled() const { return impl_->SfxEnabled(); }
bool AudioSystem::MusicEnabled() const { return impl_->MusicEnabled(); }

#else

class AudioSystem::Impl {};
AudioSystem::AudioSystem() : impl_(new Impl()) {}
AudioSystem::~AudioSystem() { delete impl_; }
bool AudioSystem::Init(const std::string&) { return false; }
void AudioSystem::ReloadConfig() {}
void AudioSystem::PlayEvent(const std::string&) {}
void AudioSystem::SetMusicForMap(int) {}
void AudioSystem::ToggleSfx() {}
void AudioSystem::ToggleMusic() {}
bool AudioSystem::SfxEnabled() const { return false; }
bool AudioSystem::MusicEnabled() const { return false; }

#endif
