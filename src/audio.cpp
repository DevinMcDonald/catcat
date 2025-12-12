#include "audio.hpp"

#include <memory>
#include <algorithm>
#include <random>
#include <string>
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
  std::vector<std::string> intro_files;
  float volume = 1.0F;
  float loop_start_sec = -1.0F;  // optional
  float loop_end_sec = -1.0F;    // optional
  float intro_start_sec = -1.0F; // optional slice start
  float intro_end_sec = -1.0F;   // optional slice end
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

  static void IntroEnded(void* user_data, ma_sound* /*sound*/) {
    if (user_data == nullptr) return;
    static_cast<Impl*>(user_data)->OnIntroEnded();
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
    const size_t idx =
        list.size() == 1 ? 0U : static_cast<size_t>(dist_(rng_) % static_cast<int>(list.size()));
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
    current_music_path_.clear();
    current_loop_start_sec_ = -1.0F;
    current_loop_end_sec_ = -1.0F;
    current_intro_start_sec_ = -1.0F;
    current_intro_end_sec_ = -1.0F;
    const auto it = music_.find(map_index);
    if (it == music_.end() || it->second.files.empty()) return;
    const auto& tracks = it->second.files;
    const size_t idx = tracks.size() == 1
                           ? 0U
                           : static_cast<size_t>(dist_(rng_) % static_cast<int>(tracks.size()));
    const std::string& path = tracks[idx];
    current_music_path_ = path;
    current_loop_start_sec_ = it->second.loop_start_sec;
    current_loop_end_sec_ = it->second.loop_end_sec;
    current_intro_start_sec_ = it->second.intro_start_sec;
    current_intro_end_sec_ = it->second.intro_end_sec;
    current_music_gain_ = std::clamp(it->second.volume, 0.0F, 2.0F);
    if (!it->second.intro_files.empty()) {
      const auto& intro_list = it->second.intro_files;
      const size_t intro_idx =
          intro_list.size() == 1
              ? 0U
              : static_cast<size_t>(dist_(rng_) % static_cast<int>(intro_list.size()));
      const std::string& intro_path = intro_list[intro_idx];
      if (InitIntro(intro_path)) {
        return;
      }
    }
    StartMainMusic();
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
        const std::string key = it.key();
        if (key == "game_over") {
          map_idx = -1;
        } else if (key.rfind("map_", 0) == 0) {
          try {
            map_idx = std::stoi(key.substr(4));
          } catch (...) {
            continue;
          }
        } else {
          continue;
        }
        MusicEntry entry;
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
          if (v.contains("intro")) {
            if (v["intro"].is_array()) {
              for (const auto& f : v["intro"]) {
                entry.intro_files.push_back(ResolvePath(base_dir, f.get<std::string>()));
              }
            } else if (v["intro"].is_string()) {
              entry.intro_files.push_back(ResolvePath(base_dir, v["intro"].get<std::string>()));
            }
          }
          if (v.contains("volume") && v["volume"].is_number()) {
            entry.volume = std::clamp(v["volume"].get<float>(), 0.0F, 2.0F);
          }
          if (v.contains("loop_start") && v["loop_start"].is_number()) {
            entry.loop_start_sec = v["loop_start"].get<float>();
          }
          if (v.contains("loop_end") && v["loop_end"].is_number()) {
            entry.loop_end_sec = v["loop_end"].get<float>();
          }
          if (v.contains("intro_start") && v["intro_start"].is_number()) {
            entry.intro_start_sec = v["intro_start"].get<float>();
          }
          if (v.contains("intro_end") && v["intro_end"].is_number()) {
            entry.intro_end_sec = v["intro_end"].get<float>();
          }
        }
        music_[map_idx] = std::move(entry);
      }
    }
  }

  void StopMusic() {
    if (intro_loaded_) {
      ma_sound_uninit(&intro_sound_);
      intro_loaded_ = false;
    }
    if (music_loaded_) {
      ma_sound_uninit(&music_sound_);
      music_loaded_ = false;
    }
  }

  bool InitIntro(const std::string& path) {
    constexpr ma_uint32 kFlags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;
    if (ma_sound_init_from_file(&engine_, path.c_str(), kFlags, nullptr, nullptr,
                                &intro_sound_) != MA_SUCCESS) {
      intro_loaded_ = false;
      return false;
    }
    ma_uint32 rate = 0;
    ma_sound_get_data_format(&intro_sound_, nullptr, nullptr, &rate, nullptr, 0);
    ma_uint64 length_frames = 0;
    ma_sound_get_length_in_pcm_frames(&intro_sound_, &length_frames);
    if (rate == 0) rate = 44100;
    ma_uint64 start_frame = 0;
    ma_uint64 end_frame = length_frames;
    if (current_intro_start_sec_ >= 0.0F) {
      start_frame = static_cast<ma_uint64>(current_intro_start_sec_ * static_cast<float>(rate));
      start_frame = std::min(start_frame, length_frames);
      ma_sound_seek_to_pcm_frame(&intro_sound_, start_frame);
    }
    if (current_intro_end_sec_ > 0.0F) {
      end_frame = static_cast<ma_uint64>(current_intro_end_sec_ * static_cast<float>(rate));
      end_frame = std::min(end_frame, length_frames);
      if (end_frame > start_frame) {
        ma_sound_set_stop_time_in_pcm_frames(&intro_sound_, end_frame);
      }
    }
    ma_sound_set_end_callback(&intro_sound_, IntroEnded, this);
    ma_sound_set_looping(&intro_sound_, MA_FALSE);
    ma_sound_set_volume(&intro_sound_, music_volume_ * current_music_gain_);
    ma_sound_start(&intro_sound_);
    intro_loaded_ = true;
    return true;
  }

  void OnIntroEnded() {
    if (intro_loaded_) {
      ma_sound_uninit(&intro_sound_);
      intro_loaded_ = false;
    }
    StartMainMusic();
  }

  bool StartMainMusic() {
    if (current_music_path_.empty()) return false;
    constexpr ma_uint32 kMusicFlags =
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;  // fully decode for seamless looping
    if (ma_sound_init_from_file(&engine_, current_music_path_.c_str(), kMusicFlags, nullptr,
                                nullptr, &music_sound_) != MA_SUCCESS) {
      music_loaded_ = false;
      return false;
    }
    ma_uint32 rate = 0;
    ma_sound_get_data_format(&music_sound_, nullptr, nullptr, &rate, nullptr, 0);
    ma_uint64 length_frames = 0;
    ma_sound_get_length_in_pcm_frames(&music_sound_, &length_frames);
    if (rate == 0) rate = 44100;
    ma_uint64 start_frame = 0;
    ma_uint64 end_frame = length_frames;
    if (current_loop_start_sec_ >= 0.0F) {
      start_frame = static_cast<ma_uint64>(current_loop_start_sec_ * static_cast<float>(rate));
    }
    if (current_loop_end_sec_ > 0.0F) {
      end_frame = static_cast<ma_uint64>(current_loop_end_sec_ * static_cast<float>(rate));
      end_frame = std::min(end_frame, length_frames);
    }
    if (start_frame < end_frame) {
      ma_data_source_set_loop_point_in_pcm_frames(ma_sound_get_data_source(&music_sound_),
                                                  start_frame, end_frame);
    }
    if (start_frame > 0) {
      ma_sound_seek_to_pcm_frame(&music_sound_, start_frame);
    }
    ma_sound_set_looping(&music_sound_, MA_TRUE);
    ma_sound_set_volume(&music_sound_, music_volume_ * current_music_gain_);
    ma_sound_start(&music_sound_);
    music_loaded_ = true;
    return true;
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
  bool intro_loaded_ = false;
  ma_sound music_sound_{};
  ma_sound intro_sound_{};
  std::unordered_map<std::string, EventEntry> events_;
  std::unordered_map<int, MusicEntry> music_;
  std::string config_path_;
  std::string current_music_path_;
  float current_loop_start_sec_ = -1.0F;
  float current_loop_end_sec_ = -1.0F;
  float sfx_volume_ = 1.0F;
  float music_volume_ = 1.0F;
  bool sfx_enabled_ = true;
  bool music_enabled_ = true;
  float current_music_gain_ = 1.0F;
  float current_intro_start_sec_ = -1.0F;
  float current_intro_end_sec_ = -1.0F;
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
