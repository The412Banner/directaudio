# DirectAudio

**Native Wine → Android AAudio audio driver.** A Wine [`mmdevapi`](https://gitlab.winehq.org/wine/wine/-/tree/master/dlls/mmdevapi) backend (`winedirectaudio.drv`) that carries guest **WASAPI** audio straight to Android **AAudio** — **no PulseAudio daemon and no ALSA server** in the path. All per-stream mixing happens **in-process, inside the driver**, so there's one fewer process and one fewer IPC hop before Android's own mixer (AudioFlinger), which every app goes through anyway.

![Pulse vs ALSA vs DirectAudio — how guest audio reaches the Android speaker](docs/audio-paths.jpg)

```
game (guest WASAPI) → winedirectaudio.drv → in-process mixer → AAudio → AudioFlinger → 🔊
                                            (sum voices, mono/5.1/7.1 → stereo, resample → 48 kHz)
```

## Status
Device-proven **v1** (arm64ec / bionic). Opt-in; pulse/alsa remain the recommended defaults. Output: 48 kHz · float · stereo.

## License & attribution
**LGPL-2.1-or-later** (see [`COPYING`](COPYING)). This is not a relicensing choice — the driver derives from and links Wine's LGPL `mmdevapi` internals (`wine/unixlib.h`, `../mmdevapi/unixlib.h`) and is modelled on `winecoreaudio.drv`, so it must remain LGPL.

**Copyright © 2026 The412Banner.** If you copy, modify, use, or distribute this code or any part of it, LGPL-2.1 **requires** you to:
- **keep the copyright notice** in [`directaudio.c`](directaudio.c) intact in every copy and derivative,
- **include the LGPL-2.1 license** ([`COPYING`](COPYING)) with any distribution,
- **make the library source available** and **state your changes** (with dates).

**Requested (courtesy):** projects that ship DirectAudio, in whole or part, are asked to credit it as *"DirectAudio by The412Banner (https://github.com/The412Banner/directaudio)"* in their docs, About screen, or release notes. See [`NOTICE`](NOTICE) and [`AUTHORS`](AUTHORS).

## This repo is a Wine DLL directory
It is **not** a standalone buildable project — a Wine driver is a PE + unixlib pair compiled by Wine's own build system against private Wine headers, and its unixlib ABI is pinned to a specific Wine base. This repo is consumed as a **git submodule** dropped in at `dlls/winedirectaudio.drv/` of a Wine/Proton tree.

### Consuming it (proton-wine or any Wine fork)
```sh
git submodule add https://github.com/The412Banner/directaudio dlls/winedirectaudio.drv
```
Then apply the two small integration deltas that live **outside** this directory and therefore cannot ship in the submodule (see [`INTEGRATION.md`](INTEGRATION.md)):
- `configure.ac` — `--with-aaudio` arg, `aaudio/AAudio.h` detection, and `WINE_CONFIG_MAKEFILE(dlls/winedirectaudio.drv)`.
- `dlls/mmdevapi/main.c` — add `directaudio` to `default_list` (omit to keep it opt-in via the `HKCU\Software\Wine\Drivers` `Audio` registry value).

### Pulling a new version into a consumer
```sh
git -C dlls/winedirectaudio.drv fetch --tags
git -C dlls/winedirectaudio.drv checkout directaudio-v2   # pin to a tagged, ABI-matched release
git add dlls/winedirectaudio.drv && git commit -m "bump directaudio → v2"
```
The consumer always builds from a **pinned** driver commit — reproducible, never a moving target.

## ABI pinning — the one hard constraint
Each tagged release is **ABI-matched to a Wine base** (the `mmdevapi` unixlib vtable must match the `mmdevapi.dll` it ships with). Tags are named accordingly, e.g. `directaudio-v1` · Wine 11.0. New *driver logic* builds fine against the pinned base; a new *Wine base* means re-pinning and rebuilding.

## Contributing
Fork this small repo (no full Proton checkout needed), open a PR against `directaudio.c`. CI compiles the DLL against the pinned Wine base to prove it builds and links the ABI. On-device audio behaviour is verified separately (no audio device in CI). Contributions are LGPL-2.1-or-later; add yourself to `AUTHORS`.
