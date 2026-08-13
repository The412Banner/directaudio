/* DirectAudio relay helper (PoC).
 *
 * A tiny standalone process that owns the AAudio output stream on behalf of the
 * DirectAudio driver. The driver (inside the FEX game process) mixes guest audio
 * into a shared memfd ring and spawns this helper; the helper's AAudio callback
 * drains the ring and plays it. Because the AAudio/AudioFlinger client now lives
 * in THIS process, not the game's, it cannot contend with the game's render
 * threads -- which is what black-screened heavy DX12/VKD3D titles (God of War)
 * when DirectAudio opened AAudio in-process.
 *
 * argv[1] = the inherited memfd file descriptor number for the shared ring.
 *
 * Build (NDK): aarch64-linux-android28-clang directaudio-relay.c -o directaudio-relay -laaudio -llog
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <aaudio/AAudio.h>
#include <android/log.h>

#include "da_relay_ring.h"

#define RLOG(...) __android_log_print(ANDROID_LOG_INFO, "DA-Relay", __VA_ARGS__)

static struct da_ring *g_ring;
static size_t          g_map_size;

/* AAudio pulls from the shared ring on the helper's own (out-of-game) audio thread. */
static aaudio_data_callback_result_t relay_cb(AAudioStream *s, void *user,
                                              void *audioData, int32_t numFrames)
{
    struct da_ring *r = g_ring;
    float *out = audioData;
    uint32_t cap = r->cap_frames;
    uint32_t ridx = __atomic_load_n(&r->ridx, __ATOMIC_ACQUIRE);
    uint32_t avail = __atomic_load_n(&r->widx, __ATOMIC_ACQUIRE) - ridx;
    uint32_t n = (uint32_t)numFrames < avail ? (uint32_t)numFrames : avail;
    uint32_t i;
    (void)s; (void)user;

    for (i = 0; i < n; i++)
    {
        uint32_t slot = (ridx + i) % cap;
        out[2*i]     = r->data[2*slot];
        out[2*i + 1] = r->data[2*slot + 1];
    }
    /* underrun -> silence the tail (keeps the stream alive) */
    for (; i < (uint32_t)numFrames; i++) { out[2*i] = 0.0f; out[2*i + 1] = 0.0f; }

    __atomic_store_n(&r->ridx, ridx + n, __ATOMIC_RELEASE);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int main(int argc, char **argv)
{
    int fd;
    struct da_ring hdr_probe;
    AAudioStreamBuilder *builder = NULL;
    AAudioStream *stream = NULL;
    aaudio_result_t r;
    pid_t orig_ppid = getppid();

    if (argc < 2) { RLOG("usage: directaudio-relay <memfd>"); return 2; }
    fd = atoi(argv[1]);

    /* Read the header first to learn the ring capacity, then map the whole thing. */
    if (pread(fd, &hdr_probe, sizeof(hdr_probe), 0) != (ssize_t)sizeof(hdr_probe) ||
        hdr_probe.magic != DA_RING_MAGIC)
    {
        RLOG("bad ring header on fd %d (magic=%#x)", fd, hdr_probe.magic);
        return 3;
    }
    g_map_size = sizeof(struct da_ring) +
                 (size_t)hdr_probe.cap_frames * DA_RING_CHANNELS * sizeof(float);
    g_ring = mmap(NULL, g_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_ring == MAP_FAILED) { RLOG("mmap ring failed"); return 4; }

    RLOG("relay start: cap=%u target=%u rate=%u ppid=%d",
         g_ring->cap_frames, g_ring->target_frames, g_ring->rate, orig_ppid);

    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, DA_RING_RATE);
    AAudioStreamBuilder_setChannelCount(builder, DA_RING_CHANNELS);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, relay_cb, NULL);

    r = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK) { RLOG("openStream failed: %d", r); return 5; }

    r = AAudioStream_requestStart(stream);
    if (r != AAUDIO_OK) { RLOG("requestStart failed: %d", r); AAudioStream_close(stream); return 6; }

    RLOG("relay playing: perf=%d burst=%d rate=%d",
         AAudioStream_getPerformanceMode(stream),
         AAudioStream_getFramesPerBurst(stream), AAudioStream_getSampleRate(stream));

    /* Idle until the producer asks us to quit or the game process disappears
     * (reparented to init) -- so we never leak an orphan relay. */
    while (!__atomic_load_n(&g_ring->quit, __ATOMIC_ACQUIRE) && getppid() == orig_ppid)
    {
        struct timespec ts = { 0, 100 * 1000 * 1000 };  /* 100 ms */
        nanosleep(&ts, NULL);
    }

    RLOG("relay stopping (quit=%u ppid=%d)", g_ring->quit, getppid());
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    munmap(g_ring, g_map_size);
    return 0;
}
