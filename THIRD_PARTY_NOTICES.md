# Third-party notices

KAMO is licensed under the MIT License (see `LICENSE`). It includes code from the following
open-source projects, each under its own license.

## KNID

KAMO versions up to 0.1.x were built on the source code of KNID
(https://github.com/sweatyeggs69/KNID) by sweatyeggs69: its GBA runtime, frontend, updater, tools
and packaging. That code was removed when KAMO moved to static recompilation; it remains in the
repository history under this license.

```
MIT License

Copyright (c) 2026 Kyle McClain

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

This license covers the KNID source code only. KNID does not include and is not
licensed to distribute any game, BIOS, artwork, music or other content owned by
Nintendo or HAL Laboratory. See THIRD_PARTY_NOTICES.md for bundled open-source
components.
```


## GBARecomp

The recompiled build in `recomp/` uses GBARecomp (https://github.com/mstan/gbarecomp) by
Matthew Stanley as the `external/gbarecomp` submodule: the ARM/Thumb recompiler that translates
the player's ROM and BIOS to C++, and the runtime the recompiled game runs on.

GBARecomp is licensed under the PolyForm Noncommercial License 1.0.0
(https://polyformproject.org/licenses/noncommercial/1.0.0). The full text is in
`external/gbarecomp/LICENSE` and in the `licenses` folder of each download. Builds that include
GBARecomp may only be used and shared for noncommercial purposes. KAMO's own source code stays
under the MIT License.

GBARecomp includes these components, each under its own license (texts in their folders under
`external/gbarecomp/external/`):

| Component | License |
|---|---|
| [arm-recomp-core](https://github.com/mstan/arm-recomp-core) | MIT, Copyright (c) 2026 Matthew Stanley |
| [recomp-net](https://github.com/RetroPortingToolKit/recomp-net) (netplay) | MIT, Copyright (c) 2026 recomp-net contributors |
| [rbengine](https://github.com/RetroPortingToolKit/rbengine) (rollback) | MIT, Copyright (c) 2026 retcomm-rbengine contributors |
| recomp-audio-core, recomp-input-core, recomp-persistence-core, recomp-diagnostics-core | MIT, Copyright (c) 2026 Matthew Stanley |
| recomp-presentation-core | MIT, Copyright (c) 2026 Alex Vanderveen / TechnicallyComputers |
| recomp-mod-core | PolyForm Noncommercial 1.0.0 |

The recompiled build links SDL2 and zlib from the MSYS2 MinGW toolchain (licenses below), and
the GCC runtime libraries (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`), which
are covered by the GCC Runtime Library Exception..

## recomp-ui

The launcher window and online lobby of the recompiled build (`external/recomp-ui`).
https://github.com/mstan/recomp-ui. MIT License, Copyright (c) 2026 Matthew Stanley; full text
in `external/recomp-ui/LICENSE`. It bundles, each under its own license:

- Dear ImGui (MIT), stb_image, stb_image_write and stb_truetype (MIT / public domain),
  tinyfiledialogs (zlib, Copyright (c) 2014-2025 Guillaume Vareille), in `src/third_party/`.
- Lato (Copyright (c) 2010-2015 Łukasz Dziedzic) and Noto Sans Symbols 2 (Copyright The Noto
  Project Authors) fonts, and country flags rendered from Noto Color Emoji, under the SIL Open
  Font License 1.1.
- OpenMoji emoji font, Copyright OpenMoji Project, under CC BY-SA 4.0.

Details are in `assets/common/fonts/NOTICE.md` and `assets/common/img/NOTICE.md` of recomp-ui,
copied into the `licenses` folder of each download.

The online lobby connects to the public lobby server `netplay.retcomm.net`, run by the
recomp-net project. It receives the game name, the build version, a fingerprint of the ROM
(not the ROM itself) and the player name you choose.

## SDL2

Simple DirectMedia Layer 2, shipped as `SDL2.dll` with the Windows download (MSYS2 MinGW build). https://libsdl.org

```
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## zlib

Linked into the recompiled build from the MSYS2 MinGW toolchain. https://zlib.net

```
Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```
