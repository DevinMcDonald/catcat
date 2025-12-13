# cat cat

A colorful, fast-paced terminal tower defense inspired by Bloons, built with [FTXUI](https://github.com/ArthurSonzogni/ftxui) and modern C++20. Deploy an ever-growing roster of cats to stop waves of mice, rats, big rats, and the occasional terrifying dog from reaching your burrow.

## Features

- **Rich TUI**: Smooth 60 FPS updates, bright colors, bold effects, and overlays for range, beams, shockwaves, and area attacks.
- **Diverse towers**:
  - `1` Default Cat — multi-target when upgraded.
  - `2` Fat Cat — 2×2 AOE, bigger range when upgraded.
  - `3` Kitty Cat — swipe cone with knockback chance when upgraded.
  - `4` Thundercat — laser; upgraded fires rapidly.
  - `5` Catatonic — purple sleep pulse; upgraded extends sleep/radius.
  - `6` Galacticat — cosmic cone; upgraded map-wide pulse every ~10s.
- **Enemies**: Mice, Rats, Big Rats, and Dogs with lane offsetting on wide paths, color-coded health, and scaling difficulty across 10 maps (100 waves).
- **Shop and unlocks**: Spend kibbles to unlock towers (10× cost) and place them (base cost). Aligned UI, hides unlocked towers.
- **Upgrades**: Press `u` on a tower to upgrade (5× base cost), unlocking unique behaviors instead of just stat buffs.
- **Audio**: SFX/music via miniaudio with JSON-configurable events and per-map music.
- **Dev mode**: `--dev` unlocks everything with plenty of kibbles and lets you skip maps with `>` (Shift+Right).

## Controls

- **Movement**: Arrows / WASD
- **Place**: `space` / `c` (uses selected tower)
- **Pick up / Move**: `m`
- **Upgrade**: `u` (5× cost)
- **Sell**: `x` (60% refund)
- **Toggle overlay / cancel**: `esc`
- **Select towers**: `1`–`6` (ordered by cost)
- **Shop panel**: `p`
- **Controls panel**: `h`
- **Next wave / auto**: `n` / `N`
- **Fast forward**: `f` (x5)
- **Audio toggles**: `t` (SFX), `y` (music)
- **Dev only**: `>` skips to next map
- **Quit**: `q`

## Build & Run

```bash
cmake -S . -B build
cmake --build build
./build/catcat           # normal
./build/catcat --dev     # dev mode (unlocks, lots of kibbles)
# Packaging: the build now also drops `build/catcat_bundle.zip` with the binary,
# `audio.json`, and the `audio/` folder. Re-run `cmake --build build --target package_catcat`
# if you want to refresh the bundle without a full rebuild.
```

### Dependencies

- CMake 3.20+
- A C++20 compiler
- FetchContent pulls FTXUI and nlohmann_json; miniaudio is downloaded as a single header at configure time. No manual setup required.

## Audio Configuration

- The game loads `audio.json` from the working directory (or alongside the binary). Events and music accept arrays or `{ "files": [...], "volume": <0-2> }`.
- Missing entries simply don’t play.
- Sample `audio.json` is included with placeholder paths; drop your own files into `audio/sfx` and `audio/music` or adjust the JSON to match your layout.

Key events you can set: `rat_die`, `tower_default_shoot`, `tower_thunder_shoot`, `tower_fat_shoot`, `tower_kitty_shoot`, `tower_catatonic_shoot`, `tower_galactic_shoot`, `wave_start`, `map_change`, `life_lost`, `unlock`, `place`, `sell`.  
Music keys: `map_0` … `map_9`, `game_over`.

## Gameplay Notes

- Kibbles now persist between maps; lives reset to 9 each map. Difficulty ramps per map and wave tier.
- Enemies can sleep (Catatonic), get knocked back (Kitty upgrade), or be vaporized en masse (Galactic upgrade).
- Range overlays always show; placed towers tint subtly, preview shows brighter.
- Auto waves stop at map transitions; pressing `N` resumes on the new map.

## Extending

- Add towers via `Tower::Type`, `GetDef`, and `SortedDefs` (cost-ordered lists drive keys/UI).
- Audio: add new event keys to `audio.json`; code-side, call `audio_->PlayEvent("your_key")`.
- Maps: edit `BuildMaps()` anchors/colors/path widths to inject new layouts.

## License

Project code: MIT.  
Bundled/placeholder audio: CC0 (see `audio.json` sample and included pack INFO).  
FTXUI, nlohmann_json, and miniaudio are under their respective licenses.
