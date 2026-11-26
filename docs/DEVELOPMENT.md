# How OLED Guard was built

**English** · [Italiano](DEVELOPMENT.it.md)

OLED Guard was built in four days (21-24 November 2026) with
[Claude Code](https://claude.com/claude-code), Anthropic's coding agent. Claude Code
wrote the code. I did the engineering around it: I defined the product and its
constraints, approved every design and plan before any code existed, made the calls
when there was a real trade-off, accepted each milestone on real hardware and decided
what shipped.

This page shows how that worked, with links to the evidence: specs, plans, commits
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

## What the real problems taught me

- **A compiler bug caught before the release.** The first CI run on GitHub failed on
  one of the two Windows images: a statistics test got 18.35 instead of 100, while
  everything passed on my PC. The first fix was based on a wrong guess, and CI
  rejected it. So we stopped guessing: CI printed the data, then the generated
  assembly. The newest Microsoft compiler (MSVC 14.52) built a position `{x, y}`
  wrongly when `x` was a negative loop counter, turning `y` into -1. The app was
  affected too: the Statistics page would have shown a wrong position map. The fix
  avoids that pattern, new tests check the map cell by cell, and the release is built
  with a compiler that gets it right
  ([fix](https://github.com/fedefreak92/oled-guard/commit/f1cd25a)).
- **Dimming the wrong areas.** The first dimming passed every synthetic test, but on my
  real desktop it darkened the wrong zones. We added a diagnostics mode (statistics,
  grid dumps, snapshots compared frame by frame), and from then on a visual feature was
  done only when it looked right on the real screen
  ([diagnostics](https://github.com/fedefreak92/oled-guard/commit/3fe5160)).
- **The app covering itself.** In the live test of 1.2.0, the pause for protected
  windows (password managers, protected video) never ended while OLED Guard's own
  window was on top of them: the code ignored the app's windows completely, even as
  covers. The log showed it within minutes, and it was fixed the same day
  ([fix](https://github.com/fedefreak92/oled-guard/commit/167b396)).
- **Cutting a feature.** "Turn off display" was built and tested, but on my TV it did
  nothing useful, and I realized I did not need it. I removed it instead of shipping
  something that fails on the main target
  ([removal](https://github.com/fedefreak92/oled-guard/commit/981befa)).
- **Thinking about the user's evening.** The final review noticed that the "display not
  found" notice would fire every evening for people who turn the TV off and leave the
  PC on, which is the main audience. Now it appears only if the display was never seen
  since the app started
  ([fix](https://github.com/fedefreak92/oled-guard/commit/017986b)).
- **Saying what is not tested.** I chose not to set up a Windows 10 virtual machine, so
  CI checks the build, the tests and the startup on the Windows 10 generation instead.
  The overlay itself has not run on a real Windows 10 PC, and the README says so.

## Numbers

- 4 days, 110 commits, 6 specs and 6 plans
- About 20,000 lines of C++20, HLSL and JavaScript, about 4,900 of them tests
- 339 automated tests, CI on two Windows versions
- 5 languages, nothing to install beyond Windows and WebView2

## Commits

Every commit carries a `Co-Authored-By: Claude` line. I kept them on purpose: they
describe exactly how the project was made.
