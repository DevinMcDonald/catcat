#pragma once

#include <optional>
#include <string>

struct UpdatePrefs {
  std::string skip_version; // normalized (leading v/V stripped)
};

std::string NormalizeVersion(const std::string &v);
UpdatePrefs LoadPrefs();
void SavePrefs(const UpdatePrefs &prefs);

bool HasNetworkConnectivity();
std::optional<std::string> DetectLatestViaBrew();
