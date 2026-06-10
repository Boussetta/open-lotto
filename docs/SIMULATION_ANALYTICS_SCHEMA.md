# Simulation Analytics Output Schema

The simulation analytics output schema is versioned and starts at:

- `simulation-analytics/v1`

Canonical schema file:

- `docs/schemas/simulation_analytics_v1.schema.json`

## Compatibility Policy

- Additive fields are allowed within `v1`.
- Field removals or semantic changes require a new schema version.
- Consumers should validate `schema_version` before parsing.

## Required Top-Level Fields

- `schema_version`
- `metadata`
- `core`
- `advanced`

## Validation in CI

A lightweight contract test checks that:

- the schema file exists
- the schema version constant matches `simulation-analytics/v1`
- metadata JSON generation includes `schema_version`
