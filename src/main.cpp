#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "game/game.h"
#include "version/version.h"

int main(int argc, const char *argv[]) {
  bool dev_mode = false;
  bool show_version = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--dev") {
      dev_mode = true;
    } else if (std::string(argv[i]) == "--version") {
      show_version = true;
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
  auto component = MakeGameComponent(screen, dev_mode);
  screen.Loop(component);
  return 0;
}
