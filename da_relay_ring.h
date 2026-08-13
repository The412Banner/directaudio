/* DirectAudio relay — shared SPSC ring layout (PoC).
 *
 * Shared between the driver (producer, runs inside the FEX game process) and the
 * relay helper (consumer, a separate process that owns the AAudio output stream).
 * Backed by a memfd created by the driver and inherited across posix_spawn, so no
 * filesystem path is involved (proot-safe) and the child gets a clean address
 * space (no fork-lock hazard). One producer, one consumer: lock-free.
 *
 * Fixed format = 48 kHz / float32 / stereo (the driver's existing mix output).
 */
#ifndef DA_RELAY_RING_H
#define DA_RELAY_RING_H

#include <stdint.h>

#define DA_RING_MAGIC     0x44415247u   /* 'DARG' */
#define DA_RING_RATE      48000
#define DA_RING_CHANNELS  2

struct da_ring
{
    uint32_t magic;
    uint32_t cap_frames;     /* ring capacity in stereo frames (power-of-two not required) */
    uint32_t target_frames;  /* producer keeps ~this many frames buffered (latency knob) */
    uint32_t rate;           /* 48000 */
    volatile uint32_t widx;  /* producer: total frames written (monotonic) */
    volatile uint32_t ridx;  /* consumer: total frames read (monotonic) */
    volatile uint32_t quit;  /* set by producer at teardown; consumer exits */
    uint32_t _pad;
    float    data[];         /* cap_frames * DA_RING_CHANNELS floats */
};

static inline uint32_t da_ring_avail(const struct da_ring *r)
{
    uint32_t w = __atomic_load_n(&r->widx, __ATOMIC_ACQUIRE);
    uint32_t rd = __atomic_load_n(&r->ridx, __ATOMIC_ACQUIRE);
    return w - rd;   /* unsigned wrap-safe as long as widx >= ridx logically */
}

#endif /* DA_RELAY_RING_H */
