#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "ai/ai_driver.h"
#include "game/game.h"
#include "version/version.h"

// ── Headless training mode (--ai --train) ────────────────────────────────────

static std::atomic<bool> g_train_running{true};
static void TrainSignalHandler(int) { g_train_running = false; }

// Crash handler: print a raw backtrace via write() (async-signal-safe).
// Requires -rdynamic or -Wl,--export-dynamic at link time for symbol names.
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <unistd.h>
static void CrashHandler(int sig) {
    const char *msg = "catcat: fatal signal — stack trace:\n";
    std::ignore = write(STDERR_FILENO, msg, __builtin_strlen(msg));
    void *frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(1);
}
#endif

static void RunHeadlessTraining(const std::string &weights_path, bool fresh,
                                int candidates, int eval_games) {
    std::signal(SIGINT, TrainSignalHandler);
    std::signal(SIGTERM, TrainSignalHandler);
#if defined(__linux__) || defined(__APPLE__)
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
#endif

    std::printf("catcat headless training | %dc x %dg | %s\n", candidates,
                eval_games, weights_path.c_str());
#ifdef ENABLE_AUDIO
    std::printf(
        "Note: built with audio. If this crashes on a server, rebuild:\n");
    std::printf(
        "  cmake -DENABLE_AUDIO=OFF -DBUILD_TESTS=OFF .. && cmake --build .\n");
#endif
    std::printf(
        "Ctrl+C or SIGTERM to stop. Weights auto-saved on improvement.\n");
    std::fflush(stdout);

    AIStats stats;
    Trainer trainer(weights_path, fresh, candidates, eval_games);
    const auto t0 = std::chrono::steady_clock::now();

    while (g_train_running) {
        trainer.RunBatch(stats, g_train_running);
        if (!g_train_running)
            break;

        const int ep = stats.episodes.load();
        const float mins =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - t0)
                .count() /
            60.0f;
        const float epm = mins > 0.05f ? static_cast<float>(ep) / mins : 0.0f;

        // Last batch waves and fitness from ring buffers
        const int wi = (stats.waves_head.load() + AIStats::kHistLen - 1) %
                       AIStats::kHistLen;
        const float last_waves =
            stats.waves_history[static_cast<std::size_t>(wi)].load();
        const int best_w = stats.best_waves.load();

        const int fi = (stats.history_head.load() + AIStats::kHistLen - 1) %
                       AIStats::kHistLen;
        const float last_fit =
            stats.history[static_cast<std::size_t>(fi)].load();
        const float best_fit = stats.best_fitness.load();

        const int wri = (stats.win_rate_head.load() + AIStats::kHistLen - 1) %
                        AIStats::kHistLen;
        const float wr =
            stats.win_rate_history[static_cast<std::size_t>(wri)].load();

        std::printf(
            "ep=%6d  epm=%5.0f  waves=%5.1f b:%-3d  fit=%6.0f b:%-6.0f  "
            "wr=%4.1f%%  s=%.3f\n",
            ep, epm, last_waves, best_w, last_fit,
            best_fit < -1e8f ? 0.0f : best_fit, wr * 100.0f,
            stats.sigma.load());
        std::fflush(stdout);
    }

    std::printf("Stopped at %d episodes. Weights saved.\n",
                stats.episodes.load());
    std::fflush(stdout);
}

int main(int argc, const char *argv[]) { // NOLINT(bugprone-exception-escape)
    bool dev_mode = false;
    bool ai_mode = false;
    bool ai_fast = false;
    bool ai_fresh = false;
    bool ai_train = false;
    bool show_version = false;
    int ai_candidates = 8;
    int ai_games = 10;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--dev")
            dev_mode = true;
        else if (arg == "--ai")
            ai_mode = true;
        else if (arg == "--fast")
            ai_fast = true;
        else if (arg == "--fresh")
            ai_fresh = true;
        else if (arg == "--train")
            ai_train = true;
        else if (arg == "--version")
            show_version = true;
        else if (arg == "--candidates" && i + 1 < argc)
            ai_candidates = std::stoi(argv[++i]);
        else if (arg == "--games" && i + 1 < argc)
            ai_games = std::stoi(argv[++i]);
    }

    if (ai_mode && ai_fresh) {
        const auto weights_path =
            (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME")
                                                       : ".") /
             ".config" / "catcat" / "ai_weights.bin")
                .string();
        std::printf(
            "--fresh will delete existing weights at:\n  %s\nProceed? [y/N] ",
            weights_path.c_str());
        std::fflush(stdout);
        char c = '\0';
        if (std::scanf(" %c", &c) != 1 || (c != 'y' && c != 'Y')) {
            std::printf("Aborted.\n");
            return 0;
        }
    }

    if (show_version) {
        std::printf("catcat %s\n", CurrentVersion().c_str());
        return 0;
    }
    if (CheckForUpdates() == UpdateAction::Exit) {
        return 0;
    }

    if (ai_mode && ai_train) {
        const auto weights_path =
            (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME")
                                                       : ".") /
             ".config" / "catcat" / "ai_weights.bin")
                .string();
        RunHeadlessTraining(weights_path, ai_fresh, ai_candidates, ai_games);
        return 0;
    }

    auto screen = ftxui::ScreenInteractive::Fullscreen();

    if (ai_mode) {
        const auto weights_path =
            (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME")
                                                       : ".") /
             ".config" / "catcat" / "ai_weights.bin")
                .string();
        auto component = MakeAIComponent(screen, weights_path, ai_fast,
                                         ai_fresh, ai_candidates, ai_games);
        screen.Loop(component);
    } else {
        auto component = MakeGameComponent(screen, dev_mode);
        screen.Loop(component);
    }
    return 0;
}
