# Provenance And License

This project is distributed under GNU GPL version 2 only (`GPL-2.0-only`). The
full license text is in `LICENSE`. Keep the notices below and provide the
corresponding source code when distributing binaries, as required by the
license.

This project was developed with substantial AI assistance; that does not change
the license obligations for this repository or the upstream code listed below.

The encoders in `src/protocol/logitech_protocol.cpp` and
`src/protocol/force_feedback.cpp` adapt commands and calculations from:

- new-lg4ff, `hid-lg4ff.c`, revision
  `2092db19f7b40854e0427a1b2e39eda9f8d0c3cd`,
  https://github.com/berarma/new-lg4ff
  Copyright (c) 2010 Simon Wood <simon@mungewell.org>
  Copyright (c) 2019 Bernat Arlandis <berarma@hotmail.com>
  Upstream SPDX: GPL-2.0-or-later. This adaptation is distributed as GPL v2.
- lg4ff_userspace, revision
  `d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d`,
  https://github.com/Kethen/lg4ff_userspace
  Project by Kethen, GNU GPL v2 license. `configure.c`, `switch_mode.c`,
  `driver_loops.c` and `rd_g25` are used as cross-reference material and as the
  source for the native input report layout. `tests/fixtures/rd_g25.txt` keeps
  the published descriptor, which the upstream README describes as coming from a
  G29 emulating a G25.
- xinput-ffb-driver, revision
  `f79f0fa91ce5f56ea95ce2b64e6cdd9ace6f6429`,
  https://github.com/mentalfoundry/xinput-ffb-driver
  Copyright (c) 2026 mentalfoundry. Its `IDirectInputEffectDriver`
  implementation and OEM registry layout were used as a Windows COM
  cross-reference. Upstream license: MIT.

MIT license text for xinput-ffb-driver:

```text
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Project modifications include pure C++20 APIs, explicit bounds, Windows report
framing with report ID zero, vector tests, Win32 transport, stop guards, and
DirectInput translation to the four hardware slots of the G25 with time-based
synthesis of periodic forces.

The uinput code, evdev handling and full real-time Linux effect engine are not
embedded. Local development tools under `.tools` and research checkouts under
`.research` are not part of the distributed project and are ignored by Git.
Source links and command details are documented in `docs/protocol.md`.
