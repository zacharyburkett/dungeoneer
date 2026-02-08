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
- Room role tagging
- Corridor metadata collection
- Explicit room adjacency graph (spans + neighbor list)
- Runtime metadata diagnostics (coverage/connectivity/attempts/room-graph metrics)

### `rng.h` + `src/rng.c`

Deterministic PRNG wrapper:
- Seed setup
- Integer and float random values
- Range generation helper

### `generator.h` + `src/generator/`

Generation entrypoint and method configs:
- `DG_ALGORITHM_ROOMS_AND_CORRIDORS`
- `DG_ALGORITHM_ORGANIC_CAVE`
- Rooms+corridors tuning includes room sizing, corridor width, and corridor routing mode
- Shared constraints:
  - Connectivity + outer walls
  - Floor coverage bounds
  - Room/special-room count bounds
  - Required room-role counts and entrance-exit graph distance
  - Per-role placement weights (distance/degree/leaf bonus)
  - Boss-on-leaf constraints
  - Forbidden (no-carve) regions
  - Retry attempts
- Room classification callback for special-room tagging
- Internal module split:
  - `src/generator/api.c`: Public generation API, attempt orchestration, and algorithm-specific config validation
  - `src/generator/primitives.c`: Shared math, geometry, and low-level tile helpers
  - `src/generator/connectivity.c`: Connectivity analysis, enforcement, and smoothing
  - `src/generator/constraints.c`: Shared constraint validation and forbidden-region enforcement
  - `src/generator/metadata.c`: Runtime metadata population, room-role assignment, and map-state initialization
  - `src/generator/rooms_corridors.c`: Rooms+corridors implementation
  - `src/generator/organic_cave.c`: Organic cave implementation
  - `src/generator/internal.h`: Internal contracts between generator modules

## Data flow

1. Create `dg_generate_request_t` (often via `dg_default_generate_request`).
2. Call `dg_generate`.
3. Access tiles and metadata from `dg_map_t`.
4. Destroy map with `dg_map_destroy`.

## Extensibility direction

- New algorithms should be added as dedicated files under `src/generator/` and dispatched from `src/generator/api.c`.
- Metadata can grow via `dg_map_metadata_t` while keeping backward-compatible defaults.
- Special room behavior is currently callback-based and can evolve into richer hook stages.

## Quality gates

- CTest-driven test executable validates API basics and generation invariants.
- Demo executable provides quick visual verification during algorithm iteration.
