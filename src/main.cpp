#include <filesystem>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "ai/ai_driver.h"
#include "game/game.h"
#include "version/version.h"

int main(int argc, const char *argv[]) {
  bool dev_mode     = false;
  bool ai_mode      = false;
  bool ai_fast      = false;
  bool ai_fresh     = false;
  bool show_version = false;
  int  ai_candidates = 8;
  int  ai_games      = 10;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if      (arg == "--dev")     dev_mode     = true;
    else if (arg == "--ai")      ai_mode      = true;
    else if (arg == "--fast")    ai_fast      = true;
    else if (arg == "--fresh")   ai_fresh     = true;
    else if (arg == "--version") show_version = true;
    else if (arg == "--candidates" && i + 1 < argc) ai_candidates = std::stoi(argv[++i]);
    else if (arg == "--games"      && i + 1 < argc) ai_games      = std::stoi(argv[++i]);
  }

  if (ai_mode && ai_fresh) {
    const auto weights_path =
        (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".")
         / ".config" / "catcat" / "ai_weights.bin").string();
    std::printf("--fresh will delete existing weights at:\n  %s\nProceed? [y/N] ",
                weights_path.c_str());
    std::fflush(stdout);
    char c = '\0';
    if (std::scanf(" %c", &c) != 1 || (c != 'y' && c != 'Y')) {
      std::printf("Aborted.\n");
      return 0;
    }
  }

  if (show_version) {
    CheckForUpdates(false, true);
    return 0;
  }
  if (CheckForUpdates() == UpdateAction::Exit) {
    return 0;
  }

  auto screen = ftxui::ScreenInteractive::Fullscreen();

  if (ai_mode) {
    const auto weights_path =
        (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".")
         / ".config" / "catcat" / "ai_weights.bin").string();
    auto component = MakeAIComponent(screen, weights_path, ai_fast, ai_fresh,
                                      ai_candidates, ai_games);
    screen.Loop(component);
  } else {
    auto component = MakeGameComponent(screen, dev_mode);
    screen.Loop(component);
  }
  return 0;
}
