// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "pal_config.h"
#include "pal_time.h"
#include "pal_utilities.h"

#include <assert.h>
#include <utime.h>
#include <time.h>
#include <sys/time.h>
#if HAVE_MACH_ABSOLUTE_TIME
#include <mach/mach_time.h>
#endif

/* BEGIN MONO_IO_PORTABILITY_H */

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include "config.h"

enum {
        PORTABILITY_NONE        = 0x00,
        PORTABILITY_UNKNOWN     = 0x01,
        PORTABILITY_DRIVE       = 0x02,
        PORTABILITY_CASE        = 0x04
};

#ifdef DISABLE_PORTABILITY

#define mono_portability_helpers_init()
#define mono_portability_find_file(pathname,last_exists) NULL

#define IS_PORTABILITY_NONE FALSE
#define IS_PORTABILITY_UNKNOWN FALSE
#define IS_PORTABILITY_DRIVE FALSE
#define IS_PORTABILITY_CASE FALSE
#define IS_PORTABILITY_SET FALSE

#else

void mono_portability_helpers_init_COREFX (void);
gchar *mono_portability_find_file_COREFX (const gchar *pathname, gboolean last_exists);
#define mono_portability_helpers_init() mono_portability_helpers_init_COREFX()
#define mono_portability_find_file(pathname,last_exists) mono_portability_find_file_COREFX(pathname,last_exists)

extern int mono_io_portability_helpers_COREFX;
#define mono_io_portability_helpers mono_io_portability_helpers_COREFX

#define IS_PORTABILITY_NONE (mono_io_portability_helpers & PORTABILITY_NONE)
#define IS_PORTABILITY_UNKNOWN (mono_io_portability_helpers & PORTABILITY_UNKNOWN)
#define IS_PORTABILITY_DRIVE (mono_io_portability_helpers & PORTABILITY_DRIVE)
#define IS_PORTABILITY_CASE (mono_io_portability_helpers & PORTABILITY_CASE)
#define IS_PORTABILITY_SET (mono_io_portability_helpers > 0)

#endif

/* END MONO_IO_PORTABILITY_H */

enum
{
    SecondsToMicroSeconds = 1000000,  // 10^6
    SecondsToNanoSeconds = 1000000000 // 10^9
};

static void ConvertUTimBuf(const UTimBuf* pal, struct utimbuf* native)
{
    native->actime = (time_t)(pal->AcTime);
    native->modtime = (time_t)(pal->ModTime);
}

static void ConvertTimeValPair(const TimeValPair* pal, struct timeval native[2])
{
    native[0].tv_sec = (long)(pal->AcTimeSec);
    native[0].tv_usec = (long)(pal->AcTimeUSec);
    native[1].tv_sec = (long)(pal->ModTimeSec);
    native[1].tv_usec = (long)(pal->ModTimeUSec);
}

int32_t SystemNative_UTime(const char* path, UTimBuf* times)
{
    assert(times != NULL);

    struct utimbuf temp;
    ConvertUTimBuf(times, &temp);

    int32_t result;
    while (CheckInterrupted(result = utime(path, &temp)));
    if (result == -1 && errno == ENOENT && IS_PORTABILITY_SET)
    {
        int32_t saved_errno = errno;
        char* located_filename = mono_portability_find_file(path, TRUE);

        if (located_filename == NULL)
        {
            errno = saved_errno;
            return -1;
        }

        while (CheckInterrupted(result = utime(located_filename, &temp)));
        g_free(located_filename);
    }
    return result;
}

int32_t SystemNative_UTimes(const char* path, TimeValPair* times)
{
    assert(times != NULL);

    struct timeval temp [2];
    ConvertTimeValPair(times, temp);

    int32_t result;
    while (CheckInterrupted(result = utimes(path, temp)));
    if (result == -1 && errno == ENOENT && IS_PORTABILITY_SET)
    {
        int32_t saved_errno = errno;
        char* located_filename = mono_portability_find_file(path, TRUE);

        if (located_filename == NULL)
        {
            errno = saved_errno;
            return -1;
        }

        while (CheckInterrupted(result = utimes(located_filename, temp)));
        g_free(located_filename);
    }
    return result;
}


int32_t SystemNative_GetTimestampResolution(uint64_t* resolution)
{
    assert(resolution);

#if HAVE_CLOCK_MONOTONIC
    // Make sure we can call clock_gettime with MONOTONIC.  Stopwatch invokes
    // GetTimestampResolution as the very first thing, and by calling this here
    // to verify we can successfully, we don't have to branch in GetTimestamp.
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) 
    {
        *resolution = SecondsToNanoSeconds;
        return 1;
    }
    else
    {
        *resolution = 0;
        return 0;
    }

#elif HAVE_MACH_ABSOLUTE_TIME
    mach_timebase_info_data_t mtid;
    if (mach_timebase_info(&mtid) == KERN_SUCCESS)
    {
        *resolution = SecondsToNanoSeconds * ((uint64_t)(mtid.denom) / (uint64_t)(mtid.numer));
        return 1;
    }
    else
    {
        *resolution = 0;
        return 0;
    }

#else /* gettimeofday */
    *resolution = SecondsToMicroSeconds;
    return 1;

#endif
}

int32_t SystemNative_GetTimestamp(uint64_t* timestamp)
{
    assert(timestamp);

#if HAVE_CLOCK_MONOTONIC
    struct timespec ts;
    int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(result == 0); // only possible errors are if MONOTONIC isn't supported or &ts is an invalid address
    (void)result; // suppress unused parameter warning in release builds
    *timestamp = ((uint64_t)(ts.tv_sec) * SecondsToNanoSeconds) + (uint64_t)(ts.tv_nsec);
    return 1;

#elif HAVE_MACH_ABSOLUTE_TIME
    *timestamp = mach_absolute_time();
    return 1;

#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
    {
        *timestamp = ((uint64_t)(tv.tv_sec) * SecondsToMicroSeconds) + (uint64_t)(tv.tv_usec);
        return 1;
    }
    else
    {
        *timestamp = 0;
        return 0;
    }

#endif
}

int32_t SystemNative_GetAbsoluteTime(uint64_t* timestamp)
{
    assert(timestamp);

#if  HAVE_MACH_ABSOLUTE_TIME
    *timestamp = mach_absolute_time();
    return 1;

#else
    *timestamp = 0;
    return 0;
#endif
}

int32_t SystemNative_GetTimebaseInfo(uint32_t* numer, uint32_t* denom)
{
#if  HAVE_MACH_TIMEBASE_INFO
    mach_timebase_info_data_t timebase;
    kern_return_t ret = mach_timebase_info(&timebase);
    assert(ret == KERN_SUCCESS);

    if (ret == KERN_SUCCESS)
    {
        *numer = timebase.numer;
        *denom = timebase.denom;
    }
    else
#endif
    {
        *numer = 1;
        *denom = 1;
    }
    return 1;
}
