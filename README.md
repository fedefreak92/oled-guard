# OLED Guard

[![CI](https://github.com/fedefreak92/oled-guard/actions/workflows/ci.yml/badge.svg)](https://github.com/fedefreak92/oled-guard/actions/workflows/ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

**English** · [Italiano](README.it.md) · [Español](README.es.md) · [Deutsch](README.de.md) · [Français](README.fr.md)

> **An AI-assisted engineering project.** I designed, directed and tested OLED Guard;
> the code was written by [Claude Code](https://claude.com/claude-code) from specs and
> plans I approved. [How it was built →](docs/DEVELOPMENT.md)

A Windows app that protects an OLED monitor or TV used with a PC from burn-in.
Many OLED TVs have no protection designed for PC use, where the taskbar, icons and
window borders sit on the same pixels for hours. OLED Guard captures the desktop on
the GPU and redraws it, shifted by 1-3 pixels, in a click-through overlay. It also
dims small static elements (taskbar, icons, logos) that stay bright for a long time,
and shows live signals and usage statistics.

![Overview page](docs/screenshots/overview.png)

| Statistics | Overlay settings |
| --- | --- |
| ![Statistics page with the panel wear map and risk chart](docs/screenshots/statistics.png) | ![Pixel shift, dimming and edge settings](docs/screenshots/overlay.png) |

*Screenshots of the interface in preview mode, with demo data.*

## Highlights

- **The whole image pipeline runs on the GPU.** The capture (Desktop Duplication)
  feeds HLSL compute shaders and a DirectComposition overlay that lets clicks through
  and is excluded from screen capture. The CPU never reads a desktop pixel: only a
  tiny summary comes back, asynchronously.
- **Safe by design.** A watchdog and an emergency hotkey on their own thread remove
  the overlay if anything stops responding, so the screen is never left black or
  frozen.
- **339 automated tests.** 306 unit tests cover the logic, which is kept free of
  Windows headers. 33 GPU tests run the real shaders on WARP and compare them with
  C++ reference models. CI builds, tests and starts the app on two Windows
  generations.
- **A compiler bug caught before release.** CI found that MSVC 14.52 miscompiles a
  loop over the position map. It was traced in the generated assembly and fixed with
  regression tests ([the story](docs/DEVELOPMENT.md#what-the-real-problems-taught-me),
  [the fix](https://github.com/fedefreak92/oled-guard/commit/f1cd25a)).
- **Built in four days with Claude Code**, spec first and test first, with every
  milestone accepted on real hardware ([how it was built](docs/DEVELOPMENT.md)).

## Features

- **Pixel shift**: the whole desktop moves by 1-3 px every 20-60 s, spread over
  2-5 s so text never blurs or jumps. Works in HDR and SDR with faithful colors.
- **Static-area dimming**: a compute shader finds 16x16 px cells that are bright
  and unchanged for minutes, and slowly lowers only the small ones (taskbar, icons,
  logos), leaving documents you are reading alone. The window under the mouse
  returns to normal at once.
- **Software brightness, darkened edges and away mode**: lowers the image when you
  step away from the PC.
- **Automatic pause** for games, full-screen apps, presentations, lock screen, UAC,
  session switches and **protected windows** (see below), plus per-app rules.
- **Live signals and statistics**: risk, motion and brightness for the last 60 s, a
  wear map of the panel that follows its proportions (16:9, 16:10, ultrawide,
  super-ultrawide), a 6 h / 24 h / 7 d risk chart, most used apps and a taskbar
  notification when unprotected risk stays high. Stats never leave the PC.
- **First-run setup**: choose the language and the display to protect; **Identify**
  shows a big number on each display, handy with a TV and a monitor side by side.
  Protection starts only when you confirm.
- **Five languages**: English, Italian, Spanish, German and French, following the
  Windows language or chosen in Settings.
- **Windows tweaks for OLED** (only on request, with backup and restore): dark theme,
  taskbar auto-hide, display timeout, black screensaver and wallpaper, hidden desktop
  icons and more.
- **Export diagnostics**: a readable text file to attach to bug reports.
- Tray icon, global hotkeys, autostart, single instance, logs and an emergency exit.

## Download and first run

1. Download the latest `oled-guard-<version>-windows-x64.zip` from
   [Releases](https://github.com/fedefreak92/oled-guard/releases) and check it
   against the `.sha256` file next to it if you like.
2. Unzip it anywhere and run `oled-guard.exe`. Nothing else needs to be installed:
   the C++ runtime is built in, and the WebView2 runtime for the window already comes
   with Windows 11 (on Windows 10 the app tells you where to get it, and protection
   works from the tray without it).
3. The release is not code-signed, so Windows SmartScreen shows a warning on the
   first run: choose *More info → Run anyway*.
4. Follow the three setup steps: language, the OLED display, a short summary.

## Protected windows and video

Some apps hide their windows from screen capture: protected video (for example in
streaming apps), password managers such as KeePassXC, some banking apps. Since the
overlay redraws a captured image, it would cover them with black or with the desktop
behind. OLED Guard pauses automatically while such a window is visible on the
protected display (you can turn this off in *App profiles*).

Protected video **inside a browser** can't be detected by Windows. In full screen,
protection pauses by itself anyway; in a window, use the manual pause or add a rule
for the browser in *App profiles*.

## How it works

```
Desktop Duplication (DXGI)  ──►  D3D11 texture (stays on the GPU)
                                      │
                 ┌────────────────────┼─────────────────────┐
                 ▼                    ▼                     ▼
        CSDetect / CSActivate     CSSummary            shift_ps.hlsl
        which cells are static    wear-map zones,      draws the shifted frame,
        and bright, dim levels    read back 1x/s       dimming, brightness, edges
                                  (async, tiny)              │
                                                             ▼
                              DirectComposition overlay, click-through,
                              excluded from screen capture
```

- **No desktop pixels are read by the CPU.** Only a small summary buffer comes
  back, asynchronously with `D3D11_MAP_FLAG_DO_NOT_WAIT`, so the render thread
  never waits on the GPU.
- **The real cursor is never touched**: the click offset equals the current shift
  (about 2 px), a deliberate trade-off against a software cursor that would add
  latency and could disappear after a crash.
- **Fail-safe overlay**: an emergency thread with its own hotkey and a watchdog
  removes the overlay if the UI or render thread stops responding, so the screen is
  never left frozen or black.
- **Interface**: WebView2 with plain HTML/CSS/JS, a strict CSP (`default-src 'self'`),
  no libraries and no network access. Texts live in `src/ui/i18n/<language>.json`,
  shared by the window, the tray and the notifications.

## Code layout

| Folder | Contents |
| --- | --- |
| `src/core` | Pure logic with no Windows headers: shift scheduler, pause rules, dimming model, statistics, alert, translations, startup decisions, diagnostics text, JSON, config, UI protocol |
| `src/app` | Win32, DXGI and D3D11: capture, render thread, overlay, tray, hotkeys, files, WebView2 window, display identification, diagnostics export |
| `src/app/shaders` | HLSL shaders, compiled at build time with `fxc` and embedded as headers |
| `src/ui` | The interface; `i18n/` holds the translations, `mock.js` feeds fake data when the page runs in a normal browser |
| `tests` | Unit tests for `src/core`, with a tiny framework and no dependencies |
| `tests_gpu` | Shader tests on the WARP software device |
| `tools` | Release packaging (dependency check, zip, SHA-256) |
| `docs/superpowers` | Design specs and implementation plans for each milestone ([how they were used](docs/DEVELOPMENT.md)) |

## Testing

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

configures, builds and runs both test suites through CTest.

- **Unit tests** cover the pure logic in `src/core`, including checks that every
  language file has the same keys and placeholders as English and that every text
  used by the page exists.
- **GPU tests** run the real shaders on **WARP**, Direct3D's software rasterizer, so
  they need no graphics card and run in CI. Each shader is checked against a C++
  reference model (`src/core/dim_model.*`, `summary_model.*`, `shade_model.*`) on
  synthetic scenes: HDR values far above SDR white, odd resolutions, 16:10, ultrawide
  and super-ultrawide sizes, active windows and pauses.

## Building

Requirements:

- Visual Studio 2022 or later, or the Build Tools, with the C++ desktop workload and
  a Windows 10/11 SDK (CMake and Ninja are included)
- A Direct3D 11 GPU to run the app; the tests also run without one

`build.ps1` finds Visual Studio by itself, builds into `build\` and runs the tests.
Use `-Config Debug` for a debug build and `-Clean` to start from scratch. The app is
`build\oled-guard.exe`; it needs the `ui` folder that the build copies next to it.
`tools\package-release.ps1` checks the executable and builds the release zip.

To work on the interface only, serve `src/ui` with any static server (for example
`python -m http.server --directory src/ui`) and open it in a browser: `mock.js`
supplies fake data. Useful parameters: `?lang=de` (language), `?lang=xx` (a pseudo
language 40% longer, to check the layout), `?setup=1` (first-run setup),
`?grid=21x9` (ultrawide wear map).

## Usage

OLED Guard runs in the tray and protects the display chosen at first run (you can
change it from the tray menu or the *Display* page). Default hotkeys (configurable):

| Hotkey | Action |
| --- | --- |
| `Ctrl+Alt+Shift+P` | Pause / resume |
| `Ctrl+Alt+Shift+F12` | Emergency exit |

Command-line options: `--open-ui`, `--list-monitors`, `--debug`, `--fast-shift`
(a new position every 3-5 s, for testing) and `--dim-diagnostics` (dimming statistics
and snapshots in the log folder).

Files:

- Settings, statistics, history, the backup of Windows settings and the logs in
  `%APPDATA%\oled-guard` (`config.json`, `stats.json`, `history.json`,
  `windows-backup.json`, `logs\`)
- WebView2 data in `%LOCALAPPDATA%\oled-guard\webview2`
- Autostart in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (value `OledGuard`),
  only if you enable it
- Exported diagnostics in your *Documents* folder

No administrator rights are needed. Windows settings change only when you apply
them from the *Windows for OLED* page, and *Restore as before* puts back the saved
values.

## Supported systems

- **Windows 11**: tested.
- **Windows 10 version 2004 or later**: supported, tested less. CI builds, tests and
  starts the app on Windows Server 2022 (the Windows 10 generation, build 20348), but
  the overlay and the window have not been tried on Windows 10 itself yet. Older
  versions show a message and don't start, because they lack the capture exclusion
  the overlay needs.
- Monitors and TVs, SDR and HDR, any refresh rate and resolution. Tested in depth on
  one setup (RTX 5080, LG OLED TV, HDR on): reports from other displays and GPUs are
  very welcome.

## Reporting a problem

Open an issue with the *Bug report* form: it asks for the display, the GPU, the
Windows version and the diagnostics file (*Advanced → Export diagnostics*, or the
tray menu). Read the file before attaching it: the configuration lists the app names
in your rules. Nothing is ever sent automatically.

Spanish, German and French are machine translations waiting for a native speaker's
review: corrections are welcome through the *Translation fix* form or a pull request
on `src/ui/i18n/`.

## Limitations

- One display at a time. Rotated (portrait) displays are not supported: the app
  stays paused.
- Protected video inside a browser is not detected (see above).
- Laptops: no battery-specific behavior yet.
- Comments in the code and the log are in Italian; identifiers are in English.
- The release build is not code-signed.

## Disclaimer

OLED Guard reduces how long the same pixels stay lit, but it cannot guarantee that
a panel will not burn in. Keep using your display's own panel care (pixel refresh,
and its pixel shift if it has one). It does not talk to the display or change its
firmware.

## How it was built

I built OLED Guard in four days with Claude Code as the coding agent, working as
product owner and technical lead: I set the scope and the constraints, approved every
design spec and plan before implementation, tested each milestone on real hardware
and decided what shipped. Claude Code wrote the code, the tests and the docs, test
first, with every task reviewed by separate agents.

[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) tells the whole story: who did what, the
method, the milestones and the real problems found along the way. The specs and
plans are in [`docs/superpowers`](docs/superpowers) (in Italian), and every commit
carries a `Co-Authored-By` line.

## License

Copyright (C) 2026 fedefreak92. OLED Guard is free software under the
[GNU General Public License v3.0](LICENSE) (GPL-3.0-only): you can use, study,
change and share it, and anyone who distributes it, changed or not, must do so under the
same license and together with the source code. Third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
