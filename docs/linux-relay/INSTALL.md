# DirectAudio for the Linux Steam client (relay build)

This is the driver for games that run under **Valve's ARM64 Proton inside a Linux
runtime on Android** - Bannerlator's linuxfs session with the native Steam client -
where the game process is a glibc process and cannot touch Android's audio
libraries itself.

Two pieces, both required:

| piece | what it is | where it runs |
|---|---|---|
| `aarch64-unix/winedirectaudio.so` + the two PE shells | the Wine `mmdevapi` driver, glibc build, **no AAudio inside** | in the game, inside the Linux rootfs |
| `directaudio-relay` | a small bionic program that owns the AAudio output and microphone streams | on the Android side, under the app's uid |

They meet over a unix socket. The driver connects, sends its launch config, and
gets back two shared-memory rings: one it fills with mixed game audio, one the
helper fills with microphone audio. The helper plays and records; the driver
keeps doing everything else it always did.

## Which Proton

Built inside `ValveSoftware/wine` at `proton_11.0`. The private `mmdevapi`
interface it implements is byte-identical across **Proton 11.0 (ARM64)**,
**Proton Experimental (ARM64)** and our own Wine-11 layers (checked 2026-09-19),
so this one build serves any of them. A future Proton on a Wine 12 base will
need a rebuild - the interface is not versioned, and a mismatch is silence, not
an error.

## Installing the driver next to Proton (not into it)

Steam verifies and updates its Proton depots, so files added under
`steamapps/common/Proton .../files/lib/wine/` will not survive. Put the driver
in a directory of its own and point Wine at it:

```
<somewhere>/directaudio/lib/wine/aarch64-unix/winedirectaudio.so
<somewhere>/directaudio/lib/wine/aarch64-windows/winedirectaudio.drv
<somewhere>/directaudio/lib/wine/i386-windows/winedirectaudio.drv
```

and set, for the game process:

```
WINEDLLPATH=<somewhere>/directaudio/lib/wine
```

Wine searches `WINEDLLPATH` entries with the per-arch subdirectories appended,
so both the PE shell and the unixlib are found there. In Bannerlator's runtime
the natural place to export it is the `bannerlator-proton` compatibility-tool
wrapper, which every Proton launch goes through.

## Selecting the driver

`mmdevapi` only tries the drivers named in the registry (or its built-in
`pulse,alsa,oss,coreaudio` list, which does not know about this one). Set, in the
game's prefix:

```
wine reg add "HKCU\Software\Wine\Drivers" /v Audio /d directaudio /f
```

Under Proton the prefix is `compatdata/<appid>/pfx`; run the command with that
prefix as `WINEPREFIX` using Proton's own `bin-arm64/wine`, or write the key from
the wrapper before it hands off to Proton.

## Running the helper

```
directaudio-relay --socket <path> [--log]
```

- Run it **under the app's uid**, the way the app already runs its PulseAudio
  daemon. Android checks `RECORD_AUDIO` against the calling uid, so a helper
  started any other way can play but cannot record.
- Start it **before** the game: the driver asks "is a helper there?" once, when
  Wine picks an audio driver, and falls back to unavailable if not.
- The socket path must be reachable from inside the Linux rootfs. Bannerlator
  binds the app's files directory and its runtime directory into the session at
  their own paths, so any socket under either works unchanged on both sides.
- The helper serves any number of game processes; each gets its own streams.
  It exits on `SIGTERM`; a game exiting tears down that game's streams only.

## A microphone for the Steam client itself (`--mic-fifo`)

The driver gives a **game** a microphone, because a game runs under Wine and
DirectAudio is Wine's audio driver. The **Steam client** is a native program
that never touches Wine: it reads its microphone from PulseAudio, and the
bundled PulseAudio has no Android input module, so friends voice chat sees
"No input devices detected". The helper closes that gap with one flag:

```
directaudio-relay --socket <path> --mic-fifo <fifo path>
```

It creates the named pipe if needed and writes the captured microphone into it as
**raw PCM, `s16le`, `48000` Hz, `1` channel (mono)**, always that format: when the
device grants the input at another rate (a Bluetooth headset mic is often
16 kHz) the helper resamples, so the rate PulseAudio is told is always the rate
the bytes really are. Load the matching source in PulseAudio:

```
load-module module-pipe-source source_name=DirectAudioMic file=<fifo path> format=s16le rate=48000 channels=1
set-default-source DirectAudioMic
```

**One microphone, every consumer.** There is a single input stream in the helper
and every consumer gets a copy of every block: each game's capture ring and the
pipe. In-game voice and Steam voice chat are different features and only one of
them transmits at a time, so the same mic reaching both is the expected
behaviour, not a conflict. The mic is hot while at least one consumer wants it:
a game between its first capture Start and its last capture voice going away,
the pipe while a reader is connected *and draining*. When PulseAudio suspends an
idle source it stops reading; the helper notices the pipe staying full for two
seconds, releases its share of the mic (the OS recording indicator goes off), and
probes with short silent writes until the source wakes up again. A reader that
closes the pipe is waited for again.

**What to expect in the log** (`logcat -s DA-Relay:I`): `mic-fifo: <path> ready`,
then `mic-fifo: reader connected` when the module loads, then `mic start (mic-fifo
wants it, N consumer(s))` / `mic stop` as consumers come and go.

## Telling the driver where the helper is

```
BANNER_AUDIO_DIRECT_RELAY=<the same socket path>
```

Unset, the driver looks for `$XDG_RUNTIME_DIR/directaudio-relay`.

## Everything else is unchanged

All `BANNER_AUDIO_DIRECT_*` knobs mean what they mean in the in-process build:
`_PERF`, `_ADAPTIVE`, `_DECAY`, `_MS`, `_MAXMS`, `_BF`, `_MBF`, `_EXCLUSIVE`,
`_WATCHDOG`, `_STALL_MS`, `_DECAY_*`, `_LOG`, `_PERIOD_MS`, `_MINPERIOD_MS`, and
the live-config mailbox `_RUNTIME` (the helper watches the file; it must be a
path the helper can read on the Android side). **`BANNER_AUDIO_DIRECT_MIC=1`**
exposes the capture endpoint exactly as before: the helper opens the input
stream (`VOICE_COMMUNICATION` preset) when the game connects and makes it hot on
the game's first capture start.

One thing is different: there is one extra hand-off, the ring. The helper's
callback pulls a burst from it, so the driver keeps two bursts queued (about 8 ms
on a 192-frame device) on top of the AAudio buffer the presets control, and the
helper raises that if a callback ever finds the ring short. `get_latency` reports
the sum, so a game that sizes its own buffers from it still sees the truth.

## Proving it is running

- Helper side: `logcat -s DA-Relay:I` shows `hello from <exe>`, then `open: buffer
  ... ring target ...`, then `capture open` / `capture start` for the mic.
- Game side: the session log carries `DirectAudio: relay: connected - ...`.
- The game process's `/proc/<pid>/maps` shows `winedirectaudio.so` and **no**
  `libaaudio.so` (it is in the helper's maps instead). The AudioFlinger track is
  owned by the helper's pid.
