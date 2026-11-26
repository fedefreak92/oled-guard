# Code signing policy

**Status:** OLED Guard applied for free code signing through
[SignPath Foundation](https://signpath.org) on 26 November 2026. Until the application is approved, the
release builds are **not signed**, and Windows SmartScreen shows a warning on the
first run. This page will say when signed releases start.

Once approved, the Windows release binaries will carry this attribution:

> Free code signing provided by [SignPath.io](https://about.signpath.io), certificate
> by [SignPath Foundation](https://signpath.org).

## How a release is built and signed

- Every release is built by [GitHub Actions](.github/workflows/ci.yml) from the source
  code in this repository, on a clean Windows runner, with all tests run before
  packaging. Nothing built on a personal PC is ever published or signed.
- Only `oled-guard.exe` from that automated build is signed. Its file metadata always
  says product name `OLED Guard` and the version of the release.
- Every signing request is approved by hand by an approver (below) before the signed
  release is published.

## Team roles

OLED Guard is maintained by one person.

| Role | Who |
| --- | --- |
| Committers and reviewers | [fedefreak92](https://github.com/fedefreak92) |
| Approvers | [fedefreak92](https://github.com/fedefreak92) |

Changes from anyone else (pull requests) are reviewed by a committer before they are
merged. All team members use multi-factor authentication on GitHub and SignPath.

## Privacy

This program will not transfer any information to other networked systems unless
specifically requested by the user or the person installing or operating it.

OLED Guard has no network features: settings, statistics and logs stay in
`%APPDATA%\oled-guard`, and the diagnostics file is written to *Documents* only when
you export it yourself. The window is drawn by the Microsoft Edge WebView2 Runtime,
a component of Windows that is covered by the
[Microsoft Privacy Statement](https://privacy.microsoft.com/privacystatement);
OLED Guard's pages load only local files.

## Changes to your system

OLED Guard changes Windows settings only when you apply them yourself from the
*Windows for OLED* page, after saving the previous values, and *Restore as before*
puts them back. It adds itself to Windows startup only if you turn on
*Start with Windows*. How to remove it completely is described in the README under
[Uninstall](README.md#uninstall).
