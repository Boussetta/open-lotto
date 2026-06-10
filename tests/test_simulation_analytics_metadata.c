/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#include "../include/simulation_analytics_metadata.h"
#include "test.h"
#include <string.h>

static void test_metadata_init_and_json(void)
{
    test_suite("Simulation Analytics Metadata");

    SimulationAnalyticsMetadata meta;
    char json[512];

    assert_equals(
        simulation_analytics_metadata_init(&meta, "Lotto 6aus49", 10000, 1, 42, "simulation"), 0,
        "metadata_init returns 0");
    assert_true(strcmp(meta.schema_version, SIM_ANALYTICS_SCHEMA_VERSION) == 0,
                "schema version is set");
    assert_true(strcmp(meta.game, "Lotto 6aus49") == 0, "game is set");
    assert_equals(meta.run_count, 10000, "run_count is set");
    assert_equals(meta.has_seed, 1, "has_seed is set");
    assert_equals((int)meta.seed, 42, "seed is set");
    assert_true(strchr(meta.generated_at_utc, 'T') != NULL, "timestamp is ISO-8601-like");

    assert_equals(simulation_analytics_metadata_to_json(&meta, json, sizeof(json)), 0,
                  "metadata_to_json returns 0");
    assert_true(strstr(json, "\"schema_version\":\"simulation-analytics/v1\"") != NULL,
                "json includes schema_version");
    assert_true(strstr(json, "\"run_count\":10000") != NULL, "json includes run_count");
    assert_true(strstr(json, "\"seed\":42") != NULL, "json includes seed");
}

static void test_invalid_inputs(void)
{
    SimulationAnalyticsMetadata meta;
    char json[8];

    assert_equals(simulation_analytics_metadata_init(NULL, "Lotto", 10, 0, 0, "simulation"), -1,
                  "init rejects NULL out");
    assert_equals(simulation_analytics_metadata_init(&meta, "Lotto", 0, 0, 0, "simulation"), -1,
                  "init rejects non-positive run_count");
    assert_equals(simulation_analytics_metadata_to_json(NULL, json, sizeof(json)), -1,
                  "json rejects NULL metadata");
    assert_equals(simulation_analytics_metadata_to_json(&meta, NULL, sizeof(json)), -1,
                  "json rejects NULL buffer");
}

int main(void)
{
    test_metadata_init_and_json();
    test_invalid_inputs();
    test_summary();
}
