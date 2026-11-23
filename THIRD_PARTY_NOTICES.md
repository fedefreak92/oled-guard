# Third-party notices

OLED Guard uses only the Windows SDK plus the component below.

## Microsoft Edge WebView2 SDK

- Package: `Microsoft.Web.WebView2` 1.0.4314.35 (nuget.org)
- Files in this repository: `third_party/webview2/include/*.h` and
  `third_party/webview2/x64/WebView2LoaderStatic.lib`
- License: BSD-3-Clause style, full text in
  [`third_party/webview2/LICENSE.txt`](third_party/webview2/LICENSE.txt)

Copyright (C) Microsoft Corporation. All rights reserved.

The WebView2 Runtime itself is not included: it ships with Windows 11 and is
installed separately on Windows 10.
