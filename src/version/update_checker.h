#pragma once

#include <optional>
#include <string>

struct UpdatePrefs {
  std::string skip_version; // normalized (leading v/V stripped)
};

std::string NormalizeVersion(const std::string &v);

// Returns true if v (already normalized, no leading v/V) looks like a clean
// release version — digits and dots only. Dev builds from git-describe have
// extra suffixes like "-3-gabcdef" or "-dirty", so they return false.
bool IsCleanVersion(const std::string &v);

UpdatePrefs LoadPrefs();
void SavePrefs(const UpdatePrefs &prefs);

std::optional<std::string> DetectLatestViaBrew();
