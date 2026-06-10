/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "simulation_analytics_metadata.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void copy_or_default(char *dst, size_t dst_size, const char *src, const char *fallback)
{
    const char *value = src && src[0] ? src : fallback;
    snprintf(dst, dst_size, "%s", value);
    dst[dst_size - 1] = '\0';
}

int simulation_analytics_metadata_init(SimulationAnalyticsMetadata *out, const char *game,
                                       int run_count, int has_seed, uint64_t seed,
                                       const char *source)
{
    time_t now;
    struct tm tm_utc;

    if (!out || run_count <= 0)
    {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    copy_or_default(out->schema_version, sizeof(out->schema_version), SIM_ANALYTICS_SCHEMA_VERSION,
                    SIM_ANALYTICS_SCHEMA_VERSION);
    copy_or_default(out->game, sizeof(out->game), game, "unknown");
    copy_or_default(out->source, sizeof(out->source), source, "simulation");

    out->run_count = run_count;
    out->has_seed = has_seed ? 1 : 0;
    out->seed = seed;

    now = time(NULL);
    if (now == (time_t)-1 || !gmtime_r(&now, &tm_utc))
    {
        snprintf(out->generated_at_utc, sizeof(out->generated_at_utc), "1970-01-01T00:00:00Z");
        return 0;
    }

    if (strftime(out->generated_at_utc, sizeof(out->generated_at_utc), "%Y-%m-%dT%H:%M:%SZ",
                 &tm_utc) == 0)
    {
        snprintf(out->generated_at_utc, sizeof(out->generated_at_utc), "1970-01-01T00:00:00Z");
    }

    return 0;
}

int simulation_analytics_metadata_to_json(const SimulationAnalyticsMetadata *meta, char *out,
                                          size_t out_size)
{
    int written;

    if (!meta || !out || out_size == 0)
    {
        return -1;
    }

    if (meta->has_seed)
    {
        written = snprintf(out, out_size,
                           "{\"schema_version\":\"%s\",\"generated_at\":\"%s\","
                           "\"game\":\"%s\",\"source\":\"%s\",\"run_count\":%d,"
                           "\"seed\":%llu}",
                           meta->schema_version, meta->generated_at_utc, meta->game, meta->source,
                           meta->run_count, (unsigned long long)meta->seed);
    }
    else
    {
        written = snprintf(out, out_size,
                           "{\"schema_version\":\"%s\",\"generated_at\":\"%s\","
                           "\"game\":\"%s\",\"source\":\"%s\",\"run_count\":%d}",
                           meta->schema_version, meta->generated_at_utc, meta->game, meta->source,
                           meta->run_count);
    }

    if (written < 0 || (size_t)written >= out_size)
    {
        return -1;
    }

    return 0;
}
