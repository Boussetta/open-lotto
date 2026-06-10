/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef SIMULATION_ANALYTICS_METADATA_H
#define SIMULATION_ANALYTICS_METADATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SIM_ANALYTICS_SCHEMA_VERSION "simulation-analytics/v1"

    typedef struct SimulationAnalyticsMetadata
    {
        char schema_version[32];
        char generated_at_utc[32];
        char game[64];
        char source[24];
        uint64_t seed;
        int has_seed;
        int run_count;
    } SimulationAnalyticsMetadata;

    int simulation_analytics_metadata_init(SimulationAnalyticsMetadata *out, const char *game,
                                           int run_count, int has_seed, uint64_t seed,
                                           const char *source);

    int simulation_analytics_metadata_to_json(const SimulationAnalyticsMetadata *meta, char *out,
                                              size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
