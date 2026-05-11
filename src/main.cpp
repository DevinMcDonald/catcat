#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <ftxui/component/loop.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
ftxui::Loop* g_loop = nullptr;
}

static void catcat_step() {
  if (!g_loop) return;
  if (g_loop->HasQuitted()) {
    emscripten_cancel_main_loop();
    EM_ASM({
      if (window.history.length > 1) window.history.back();
      else window.location.href = '/';
    });
    return;
  }
  g_loop->RunOnce();
}

extern "C" void catcat_resize(int cols, int rows) {
  struct winsize ws {};
  ws.ws_col = static_cast<unsigned short>(cols);
  ws.ws_row = static_cast<unsigned short>(rows);
  ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws);
}
#endif

#include "game/game.h"
#include "version/version.h"

int main(int argc, const char *argv[]) {
  bool dev_mode = false;

#ifndef __EMSCRIPTEN__
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
#else
  (void)argc;
  (void)argv;
#endif

#ifdef __EMSCRIPTEN__
  static auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto component = MakeGameComponent(screen, dev_mode);
  static ftxui::Loop loop(&screen, component);
  g_loop = &loop;
  emscripten_set_main_loop(catcat_step, 0, 0);
#else
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto component = MakeGameComponent(screen, dev_mode);
  screen.Loop(component);
#endif

  return 0;
}
