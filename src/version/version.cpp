#include "version/version.h"

#include <cctype>
#include <iostream>
#include <optional>
#include <string>

#include "version/update_checker.h"

std::string CurrentVersion() { return CATCAT_VERSION; }

UpdateAction CheckForUpdates(bool interactive_prompt, bool show_up_to_date) {
  const std::string current_raw = CurrentVersion();
  const std::string current = NormalizeVersion(current_raw);
  const auto latest = DetectLatestViaBrew();
  if (!latest.has_value() || latest->empty()) {
    if (show_up_to_date) {
      std::cout << "catcat " << current_raw
                << " (could not determine latest; check internet and run "
                   "brew update)\n";
    }
    return UpdateAction::Continue;
  }
  const std::string latest_norm = NormalizeVersion(*latest);
  if (latest_norm == current) {
    if (show_up_to_date) {
      std::cout << "catcat " << current_raw << " (up to date)\n";
    }
    return UpdateAction::Continue;
  }
  if (!interactive_prompt) {
    std::cout << "catcat " << current_raw << " (latest " << *latest
              << "). Run: brew update && brew upgrade devinmcdonald/catcat/catcat\n";
    return UpdateAction::Continue;
  }

  UpdatePrefs prefs = LoadPrefs();
  if (!prefs.skip_version.empty() && prefs.skip_version == latest_norm) {
    return UpdateAction::Continue;
  }

  std::cout << "\nA new catcat version is available.\n"
            << "Current: " << current_raw << "\n"
            << "Latest : " << *latest << "\n"
            << "[u]pdate now (brew update && brew upgrade catcat), [s]kip once, [k] skip this version: "
            << std::flush;
  std::string choice;
  std::getline(std::cin, choice);
  if (!choice.empty()) {
    const char c = static_cast<char>(std::tolower(choice[0]));
    if (c == 'u') {
      std::cout << "Run: brew update && brew upgrade devinmcdonald/catcat/catcat\n";
      return UpdateAction::Exit;
    }
    if (c == 'k') {
      prefs.skip_version = latest_norm;
      SavePrefs(prefs);
    }
  }
  return UpdateAction::Continue;
}
