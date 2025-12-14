#include "version/update_checker.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <optional>
#include <string>

using namespace std::chrono_literals;

namespace {

std::filesystem::path PrefsPath() {
  std::filesystem::path home = std::getenv("HOME") ? std::getenv("HOME") : "";
  if (home.empty()) {
    home = ".";
  }
  return home / ".config" / "catcat" / "update_prefs.cfg";
}

std::optional<std::string> StableVersionFromJson(const std::string &data) {
  const std::string key = "\"stable\"";
  const auto key_pos = data.find(key);
  if (key_pos == std::string::npos)
    return std::nullopt;
  const auto colon_pos = data.find(':', key_pos);
  if (colon_pos == std::string::npos)
    return std::nullopt;
  const auto quote_start = data.find('"', colon_pos);
  if (quote_start == std::string::npos)
    return std::nullopt;
  const auto quote_end = data.find('"', quote_start + 1);
  if (quote_end == std::string::npos || quote_end <= quote_start + 1)
    return std::nullopt;
  return data.substr(quote_start + 1, quote_end - quote_start - 1);
}

std::optional<std::string> FetchLatestForCmd(const std::string &cmd) {
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return std::nullopt;
  }
  std::string data;
  char buffer[512];
  while (true) {
    size_t n = fread(buffer, 1, sizeof(buffer), pipe);
    if (n == 0)
      break;
    data.append(buffer, buffer + n);
  }
  pclose(pipe);
  return StableVersionFromJson(data);
}

} // namespace

std::string NormalizeVersion(const std::string &v) {
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) {
    return v.substr(1);
  }
  return v;
}

UpdatePrefs LoadPrefs() {
  UpdatePrefs prefs;
  const auto path = PrefsPath();
  std::ifstream in(path);
  if (!in) {
    return prefs;
  }
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find('=');
    if (pos == std::string::npos)
      continue;
    const auto key = line.substr(0, pos);
    const auto value = line.substr(pos + 1);
    if (key == "skip_version") {
      prefs.skip_version = NormalizeVersion(value);
    }
  }
  return prefs;
}

void SavePrefs(const UpdatePrefs &prefs) {
  const auto path = PrefsPath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return;
  }
  out << "skip_version=" << prefs.skip_version << "\n";
}

bool HasNetworkConnectivity() {
  // Quick connectivity probe with 1s timeout; avoid blocking when offline.
  int ret = std::system("ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1");
  return ret == 0;
}

std::optional<std::string> DetectLatestViaBrew() {
  const auto start_time = std::chrono::steady_clock::now();
  const auto overall_timeout = std::chrono::seconds(3);

  if (!HasNetworkConnectivity()) {
    return std::nullopt;
  }

  const std::array<std::string, 2> cmds = {
      "brew info --json=v2 devinmcdonald/catcat/catcat 2>/dev/null",
      "brew info --json=v2 catcat 2>/dev/null",
  };
  for (const auto &cmd : cmds) {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed >= overall_timeout) {
      return std::nullopt;
    }
    const auto remaining = overall_timeout - elapsed;
    auto fut =
        std::async(std::launch::async, FetchLatestForCmd, std::cref(cmd));
    if (fut.wait_for(remaining) != std::future_status::ready) {
      return std::nullopt;
    }
    auto result = fut.get();
    if (result.has_value() && !result->empty()) {
      return result;
    }
  }
  return std::nullopt;
}
