# DirectAudio — Progress Log / Checkpoint

## 2026-08-12 — CHECKPOINT (resume here)

Native Wine mmdevapi backend → Android AAudio (one shared AAudio output + in-process
mixer; each guest WASAPI stream = a "voice"; a per-stream timer thread signals the
client's event handle each period).

### Shipped
- **v1.1** (tag `directaudio-v1.1`): audio switching (broadened AAudio error-callback
  recovery + stream-identity guard + no-lost-wakeup reopen gate) and guarded
  `AAudioStreamBuilder_setUsage` (weak symbol, safe on minSdk-26). Merged upstream into
  GameNative via joshuatam/GameNative#7 -> utkarshdalal/GameNative#1806.
- **v1.1.1** (tag `directaudio-v1.1.1`, main): fix `release_stream` teardown deadlock.
  A game that creates+starts+RELEASES a transient event-driven stream during init
  (God of War via FAudio/XAudio2) deadlocked its main thread on the unbounded
  `NtWaitForSingleObject(timer_thread)` join -> black-screen hang while audio played.
  Fix: atomic `please_quit` + 500 ms bounded join; on timeout unblock + leak rather than
  deadlock/UAF. Device-proven to clear THIS hang.

### Branches
- `main` / tags = ship code (clean, no logging).
- `feat/diagnostics` = latest + all logcat probes (tag `DirectAudio`, no WINEDEBUG). Not
  for release; hot-swap onto a container to trace on-device. Keep rebased on main.

### Open: God of War second stall (unsolved)
GoW gets past the release deadlock, then its MAIN thread hangs again in
`NtWaitForSingleObject` — waiting on a GoW-INTERNAL object (it makes zero driver calls
after FAudio's device-details format sweep; invisible under FEX/arm64ec). Audio plays
fine. Boots fine on winepulse/winealsa. Ruled out: latency, endpoint properties,
`get_position` (unused by GoW), `is_format_supported` PCM-vs-float (winealsa also S_OK's
it). Remaining difference is architectural (shared mixer vs per-stream backends).

### Next steps (both, later)
1. Instrument winealsa, run GoW on it (boots), capture its exact mmdevapi call sequence,
   diff vs ours -> the divergence = the specific trigger. Decisive.
2. Fix `is_format_supported` shared-mode closest-match: return S_FALSE for non-exact so
   mmdevapi's PE side (dlls/mmdevapi/client.c) hands the client the real float mix format
   (winepulse/winealsa behavior). Correctness toward making DirectAudio a full
   replacement for winepulse/winealsa. Don't regress DiRT 3 (which needed S_OK).

Long-term goal: DirectAudio becomes a drop-in REPLACEMENT for winepulse/winealsa.
