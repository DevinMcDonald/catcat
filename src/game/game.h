#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

ftxui::Component MakeGameComponent(ftxui::ScreenInteractive &screen,
                                   bool dev_mode = false);
