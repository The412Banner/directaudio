/* DirectAudio relay build: the AAudio names the shared driver code spells out,
 * without the AAudio headers.
 *
 * Copyright 2026 The412Banner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * In the relay build (DA_RELAY, the glibc unixlib for the Linux Steam client)
 * no AAudio call is made in the game process - the helper makes them all - but
 * the driver's own state still carries AAudio-typed fields and compares against
 * AAudio enum values (stream->aa_perf, fmt_to_aaudio, the result codes the
 * create_stream path checks). This header gives those names their NDK values so
 * the shared code compiles unchanged. Nothing here reaches the helper raw: the
 * hello message carries the host's 0/1/2 perf convention and flag bits, so a
 * value drift between this file and the NDK could only ever mislabel a log line.
 */
#ifndef DA_AAUDIO_COMPAT_H
#define DA_AAUDIO_COMPAT_H

#include <stdint.h>

typedef int32_t aaudio_result_t;
typedef int32_t aaudio_format_t;
typedef int32_t aaudio_performance_mode_t;
typedef int32_t aaudio_sharing_mode_t;

/* Opaque: the relay build never has one, the fields that hold it stay NULL. */
typedef struct AAudioStreamStruct AAudioStream;

enum
{
    AAUDIO_OK                    = 0,
    AAUDIO_ERROR_DISCONNECTED    = -899,
    AAUDIO_ERROR_ILLEGAL_ARGUMENT = -898,
    AAUDIO_ERROR_INTERNAL        = -896,
    AAUDIO_ERROR_INVALID_STATE   = -895,
    AAUDIO_ERROR_INVALID_HANDLE  = -892,
    AAUDIO_ERROR_UNIMPLEMENTED   = -890,
    AAUDIO_ERROR_UNAVAILABLE     = -889,
    AAUDIO_ERROR_NO_MEMORY       = -887,
    AAUDIO_ERROR_TIMEOUT         = -885,
};

enum
{
    AAUDIO_FORMAT_UNSPECIFIED    = 0,
    AAUDIO_FORMAT_PCM_I16        = 1,
    AAUDIO_FORMAT_PCM_FLOAT      = 2,
    AAUDIO_FORMAT_PCM_I24_PACKED = 3,
    AAUDIO_FORMAT_PCM_I32        = 4,
};

enum
{
    AAUDIO_PERFORMANCE_MODE_NONE         = 10,
    AAUDIO_PERFORMANCE_MODE_POWER_SAVING = 11,
    AAUDIO_PERFORMANCE_MODE_LOW_LATENCY  = 12,
};

enum
{
    AAUDIO_SHARING_MODE_EXCLUSIVE = 0,
    AAUDIO_SHARING_MODE_SHARED    = 1,
};

#endif /* DA_AAUDIO_COMPAT_H */
