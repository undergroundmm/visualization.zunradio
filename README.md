# visualization.zunradio

The full screen scene of [ZUN rádio](https://zunradio.cz/) as a [Kodi](https://kodi.tv) music
visualisation: red dots, rings and waves on black that move with the bass and the highs of any music.

[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build](https://github.com/undergroundmm/visualization.zunradio/actions/workflows/build.yml/badge.svg?branch=Omega)](https://github.com/undergroundmm/visualization.zunradio/actions/workflows/build.yml)

Together with the ZUN rádio add-on ([plugin.audio.zunradio](https://github.com/undergroundmm/plugin.audio.zunradio),
in the official Kodi repository) it also draws the red card behind the cover of the track on air,
pulsing with the bass, as on the website. The card is drawn only in the scene window of that add-on
(windows of Python add-ons, ids 13000 to 13099), never behind the skin menus or in the standard
visualisation window. The plugin never installs or selects the visualisation: choose it yourself in
Settings > Player > Music > Visualisation.

## How it works

- **Audio analysis** (`src/Scena.cpp`) runs when Kodi passes the samples (`AudioData`, in step with the
  output): the bass through two low-pass filters at 120 Hz, the highs through a high-pass filter at
  3 kHz, beats from the rise of the bass energy. Render and the skin read the state at their own time.
- **Rendering:** OpenGL and OpenGL ES (`src/Main_gl.cpp`, `resources/shaders/GL`, `resources/shaders/GLES`)
  and DirectX 11 (`src/Main_dx.cpp`, HLSL compiled with `fxc`). One quad per shape, the shape itself
  is cut out in the shader. No external dependencies.
- **Rhythm for the skin:** the pulse of the bass is reported as the active preset (`zun`, `l1` to
  `p12345678`) and the flash of the card on every 8th beat as `Visualisation.Locked`, so the scene
  window of the plugin animates the cover in step with the music.

## Branches

| Branch | Kodi | Visualization API |
|---|---|---|
| `Piers` | 22 | 5.0.0 |
| `Omega` | 21 | 4.0.0 |

## Build instructions

Use the branch that matches your Kodi version, and the same branch of Kodi.

### Linux

1. `git clone --branch Omega https://github.com/xbmc/xbmc.git`
2. `git clone --branch Omega https://github.com/undergroundmm/visualization.zunradio.git`
3. `cd visualization.zunradio && mkdir build && cd build`
4. `cmake -DADDONS_TO_BUILD=visualization.zunradio -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../../xbmc/kodi-build/addons -DPACKAGE_ZIP=1 ../../xbmc/cmake/addons`
5. `make`

The add-on files are placed in `../../xbmc/kodi-build/addons`, so a Kodi built from source and run
directly finds it as a system add-on. Windows builds the same way with the Visual Studio generator
(see `.github/workflows/build.yml`).

## License

The code is GPL-2.0-or-later, see [LICENSE.md](LICENSE.md). The add-on is published by
Underground Media Management s.r.o., the operator of ZUN rádio. The ZUN rádio name and logo in
`icon.png` and `fanart.jpg` belong to the operator and are not covered by the code licence.

Contact: info@undergroundmediamanagement.cz
