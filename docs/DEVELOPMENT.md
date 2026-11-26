# How OLED Guard was built

**English** · [Italiano](DEVELOPMENT.it.md)

I designed and shipped OLED Guard in four days (21-24 November 2026), leading the
project from start to finish and using [Claude Code](https://claude.com/claude-code),
Anthropic's coding agent, for the implementation. I defined the product and its
constraints, approved every design and plan before any code existed, made the calls
where there was a real trade-off, accepted each milestone on real hardware and decided
what shipped. Claude Code wrote the code following those specs and plans.

This page documents how that worked, with links to the evidence: specs, plans, commits
and tests.

## Who did what

| Me | Claude Code |
| --- | --- |
| The problem: my LG OLED TV, used as a PC monitor, has no burn-in protection designed for desktop use | Asked the design questions and proposed options with their trade-offs |
| Scope and priorities of each release: TVs first, five languages, Windows 10 and 11, what to cut | Wrote the design specs and the implementation plans |
| Approved every spec and every plan before implementation started | Wrote the code, the tests and the documentation, test first |
| The product decisions where the design had a real choice (examples below) | Had every task reviewed by separate agents and wrote down every deviation from the plan |
| Acceptance tests on real hardware at the end of each milestone (RTX 5080, LG OLED TV, 4K 143 Hz, HDR on) | Prepared the test checklists, read the logs and fixed what the tests found |
| Release decisions: merging, the GPL-3.0 license, publishing | Prepared the release: CI, packaging, README in five languages |

## The method

Every milestone went through the same steps, using the
[Superpowers](https://github.com/obra/superpowers) workflow for Claude Code:

1. **Design.** The agent asks one question at a time, then writes a spec
   (`docs/superpowers/specs`). Nothing is built until I approve it.
2. **Plan.** The spec becomes small tasks, each with the tests that prove it
   (`docs/superpowers/plans`). I approve the plan too.
3. **Implementation, test first.** Failing test, code, passing test, commit. Separate
   agents then review each task against the spec and for code quality.
4. **Final review** of the whole branch before merging.
5. **Live test on the TV**, with my settings backed up before and restored after. The
   app's own log is the evidence, not the feeling that it works.
6. **Merge and release** only with my OK.

## Milestones

The specs and plans are written in Italian, my language.

| Date | Milestone | Version | Spec | Plan |
| --- | --- | --- | --- | --- |
| 21 Nov | Pixel shift overlay, capture on the GPU, emergency exit | | [spec](superpowers/specs/2026-11-21-oled-guard-design.md) | [plan](superpowers/plans/2026-11-21-oled-guard.md) |
| 21 Nov | Dimming of static bright areas (compute shaders) | | [spec](superpowers/specs/2026-11-21-oled-guard-dimming-design.md) | [plan](superpowers/plans/2026-11-21-oled-guard-dimming.md) |
| 21 Nov | First interface (WebView2) | | [spec](superpowers/specs/2026-11-21-oled-guard-ui-design.md) | [plan](superpowers/plans/2026-11-21-oled-guard-ui.md) |
| 22 Nov | New interface: sidebar, 10 pages, brightness, edges, away mode | 1.0.0 | [spec](superpowers/specs/2026-11-21-oled-guard-ui-v2-design.md) | [plan](superpowers/plans/2026-11-21-oled-guard-ui-v2.md) |
| 23 Nov | Live signals, wear map and statistics | 1.1.0 | [spec](superpowers/specs/2026-11-22-oled-guard-signals-stats-design.md) | [plan](superpowers/plans/2026-11-22-oled-guard-signals-stats.md) |
| 24 Nov | Any display, five languages, Windows 10, first-run setup | 1.2.0 | [spec](superpowers/specs/2026-11-23-oled-guard-multi-setup-design.md) | [plan](superpowers/plans/2026-11-23-oled-guard-multi-setup.md) |

## Guardrails for agent-written code

An agent writes code fast, so the checks have to be fast and automatic too.

- **Logic kept apart from Windows.** Everything that decides something (when to shift,
  when to pause, how much to dim, statistics, translations, startup) lives in
  `src/core` with no Windows headers, so 306 unit tests can cover it.
- **Shaders tested without a GPU.** 33 tests run the real HLSL shaders on WARP,
  Direct3D's software rasterizer, and compare them with C++ reference models.
- **CI on two Windows generations.** Every push builds, runs all tests and starts the
  real executable on Windows 11 and on Windows Server 2022 (Windows 10 generation).
- **Translations checked by tests.** Every language must have the same keys and
  placeholders as English, and every text the page uses must exist.
- **Fail-safe by design.** An overlay bug could leave the screen black or frozen, so a
  watchdog and an emergency hotkey on their own thread remove the overlay if anything
  stops responding.
- **Written rulings.** Whenever the agent departed from the plan, it wrote down what it
  decided, why, and what it would cost if wrong. I reviewed those calls instead of
  rediscovering them in the code.

## Issues found and how they were handled

- **Compiler miscompilation caught before the release.** CI detected that MSVC 14.52,
  Microsoft's latest compiler, generated wrong code for a loop over the position map: a
  statistics test returned 18.35 instead of 100, while the same code was correct with
  MSVC 14.51 locally and 14.44 in CI. Impact: the Statistics page would have shown an
  incorrect position map. A first fix, based on a hypothesis, failed CI; the analysis
  then moved to the data, with cell values and generated assembly extracted from CI.
  Root cause: the optimizer combined a negative 64-bit loop counter into a struct
  argument without clearing its upper half. Resolution: the affected loops no longer
  build that struct, regression tests check every cell, and releases are built with
  MSVC 14.44 ([fix](https://github.com/fedefreak92/oled-guard/commit/f1cd25a)).
- **Dimming validated only on synthetic data.** The first dimming implementation passed
  every synthetic test but darkened the wrong areas on a real desktop. Resolution: a
  diagnostics mode (statistics, grid dumps, frame snapshots), and visual features are
  accepted only after verification on the real screen
  ([diagnostics](https://github.com/fedefreak92/oled-guard/commit/3fe5160)).
- **Protected-window pause not ending.** The live acceptance test of 1.2.0 showed that
  the pause for protected windows (password managers, protected video) did not end
  while OLED Guard's own window covered them: the app's windows were excluded from the
  occlusion check entirely. Found from the log during the test and fixed the same day
  ([fix](https://github.com/fedefreak92/oled-guard/commit/167b396)).
- **Feature removed after acceptance testing.** "Turn off display" worked as designed
  but brought no benefit on the main target hardware, so it was removed rather than
  shipped ([removal](https://github.com/fedefreak92/oled-guard/commit/981befa)).
- **Notification behavior reviewed against real usage.** The final review found that the
  "display not found" notice would fire every evening for users who switch off the TV
  and leave the PC on, which is the main audience. It now fires only if the display was
  never detected since the app started ([fix](https://github.com/fedefreak92/oled-guard/commit/017986b)).
- **Test coverage stated explicitly.** Windows 10 is covered by CI (build, tests and
  startup on the Windows 10 generation) rather than by a dedicated machine. The overlay
  has not yet run on a physical Windows 10 PC, and the README says so.

## Numbers

- 4 days, 113 commits, 6 specs and 6 plans
- About 20,000 lines of C++20, HLSL and JavaScript, about 4,900 of them tests
- 339 automated tests, CI on two Windows versions
- 5 languages, nothing to install beyond Windows and WebView2

## Commits

Every commit carries a `Co-Authored-By: Claude` line. I kept them on purpose: they
describe exactly how the project was made.
