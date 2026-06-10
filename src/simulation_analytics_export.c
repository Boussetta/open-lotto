/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "simulation_analytics_export.h"
#include <stdio.h>

int simulation_analytics_export_json_file(const char *filename,
                                          const SimulationAnalyticsMetadata *metadata,
                                          const SimulationAnalyticsCoreReport *core,
                                          const SimulationAnalyticsAdvancedReport *advanced)
{
    FILE *f;

    if (!filename || !metadata || !core || !advanced)
    {
        return -1;
    }

    f = fopen(filename, "w");
    if (!f)
    {
        return -1;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"schema_version\": \"%s\",\n", SIM_ANALYTICS_SCHEMA_VERSION);
    fprintf(f,
            "  \"metadata\": {\"generated_at\": \"%s\", \"game\": \"%s\", \"source\": "
            "\"%s\", \"run_count\": %d",
            metadata->generated_at_utc, metadata->game, metadata->source, metadata->run_count);
    if (metadata->has_seed)
    {
        fprintf(f, ", \"seed\": %llu", (unsigned long long)metadata->seed);
    }
    fprintf(f, "},\n");
    fprintf(f,
            "  \"core\": {\"draw_count\": %d, \"number_min\": %d, \"number_max\": %d, "
            "\"total_hits\": %d, \"mean_hits_per_number\": %.6f, "
            "\"variance_hits_per_number\": %.6f},\n",
            core->draw_count, core->number_min, core->number_max, core->total_hits,
            core->mean_hits_per_number, core->variance_hits_per_number);
    fprintf(f, "  \"advanced\": {\"entropy_normalized\": %.6f, \"hot\": [", advanced->entropy_normalized);
    for (int i = 0; i < advanced->top_n; i++)
    {
        fprintf(f, "{\"number\":%d,\"count\":%d,\"percentage\":%.6f}%s", advanced->hot[i].number,
                advanced->hot[i].count, advanced->hot[i].percentage,
                (i + 1 == advanced->top_n) ? "" : ",");
    }
    fprintf(f, "], \"cold\": [");
    for (int i = 0; i < advanced->top_n; i++)
    {
        fprintf(f, "{\"number\":%d,\"count\":%d,\"percentage\":%.6f}%s", advanced->cold[i].number,
                advanced->cold[i].count, advanced->cold[i].percentage,
                (i + 1 == advanced->top_n) ? "" : ",");
    }
    fprintf(f, "]}}\n");
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

int simulation_analytics_export_csv_file(const char *filename,
                                         const SimulationAnalyticsMetadata *metadata,
                                         const SimulationAnalyticsCoreReport *core,
                                         const SimulationAnalyticsAdvancedReport *advanced)
{
    FILE *f;

    if (!filename || !metadata || !core || !advanced)
    {
        return -1;
    }

    f = fopen(filename, "w");
    if (!f)
    {
        return -1;
    }

    fprintf(f, "section,key,value,extra\n");
    fprintf(f, "metadata,schema_version,%s,\n", SIM_ANALYTICS_SCHEMA_VERSION);
    fprintf(f, "metadata,generated_at,%s,\n", metadata->generated_at_utc);
    fprintf(f, "metadata,game,%s,\n", metadata->game);
    fprintf(f, "metadata,source,%s,\n", metadata->source);
    fprintf(f, "metadata,run_count,%d,\n", metadata->run_count);
    if (metadata->has_seed)
    {
        fprintf(f, "metadata,seed,%llu,\n", (unsigned long long)metadata->seed);
    }
    fprintf(f, "core,draw_count,%d,\n", core->draw_count);
    fprintf(f, "core,number_min,%d,\n", core->number_min);
    fprintf(f, "core,number_max,%d,\n", core->number_max);
    fprintf(f, "core,total_hits,%d,\n", core->total_hits);
    fprintf(f, "core,mean_hits_per_number,%.6f,\n", core->mean_hits_per_number);
    fprintf(f, "core,variance_hits_per_number,%.6f,\n", core->variance_hits_per_number);
    fprintf(f, "advanced,entropy_normalized,%.6f,\n", advanced->entropy_normalized);
    for (int i = 0; i < advanced->top_n; i++)
    {
        fprintf(f, "hot,number,%d,%d|%.6f\n", advanced->hot[i].number, advanced->hot[i].count,
                advanced->hot[i].percentage);
        fprintf(f, "cold,number,%d,%d|%.6f\n", advanced->cold[i].number,
                advanced->cold[i].count, advanced->cold[i].percentage);
    }

    fclose(f);
    return 0;
}
