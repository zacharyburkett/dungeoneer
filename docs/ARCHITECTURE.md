# Architecture Overview

## Repository layout

- `include/dungeoneer/`: Public API headers
- `src/`: Library implementation
- `tests/`: Test executable source
- `demos/`: Demo programs for visualization
- `docs/`: Planning and architecture docs

## Core modules

### `types.h`

Shared primitive types:
- Status/result codes
- Tile enum
- Points and rectangles

### `map.h` + `src/map.c`

Owns map storage and metadata:
- Tile buffer allocation and lifecycle
- Tile read/write helpers
- Room metadata collection

### `rng.h` + `src/rng.c`

Deterministic PRNG wrapper:
- Seed setup
- Integer and float random values
- Range generation helper

### `generator.h` + `src/generator.c`

Generation entrypoint and method configs:
- `DG_ALGORITHM_ROOMS_AND_CORRIDORS`
- `DG_ALGORITHM_ORGANIC_CAVE`
- Shared constraints (connectivity + outer walls)
- Room classification callback for special-room tagging

## Data flow

1. Create `dg_generate_request_t` (often via `dg_default_generate_request`).
2. Call `dg_generate`.
3. Access tiles and metadata from `dg_map_t`.
4. Destroy map with `dg_map_destroy`.

## Extensibility direction

- New algorithms should be added as new config structures and dispatch paths in `generator.c`.
- Metadata can grow via `dg_map_metadata_t` while keeping backward-compatible defaults.
- Special room behavior is currently callback-based and can evolve into richer hook stages.

## Quality gates

- CTest-driven test executable validates API basics and generation invariants.
- Demo executable provides quick visual verification during algorithm iteration.
