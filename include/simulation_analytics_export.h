/* SPDX-FileCopyrightText: 2026 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifndef SIMULATION_ANALYTICS_EXPORT_H
#define SIMULATION_ANALYTICS_EXPORT_H

#include "simulation_analytics_advanced.h"
#include "simulation_analytics_core.h"
#include "simulation_analytics_metadata.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int simulation_analytics_export_json_file(const char *filename,
                                              const SimulationAnalyticsMetadata *metadata,
                                              const SimulationAnalyticsCoreReport *core,
                                              const SimulationAnalyticsAdvancedReport *advanced);

    int simulation_analytics_export_csv_file(const char *filename,
                                             const SimulationAnalyticsMetadata *metadata,
                                             const SimulationAnalyticsCoreReport *core,
                                             const SimulationAnalyticsAdvancedReport *advanced);

#ifdef __cplusplus
}
#endif

#endif
