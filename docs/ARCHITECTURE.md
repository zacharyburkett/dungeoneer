# Architecture Overview

## Repository layout

- `include/dungeoneer/`: Public API headers
- `src/`: Library implementation
- `tests/`: Test executable source
- `demos/`: Demo programs for visualization
- `apps/nuklear/`: Optional Nuklear editor core and GLFW presenter
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
- Corridor metadata collection
- Room adjacency graph metadata (spans + neighbor list)
- Runtime diagnostics (coverage/connectivity/attempts)

### `io.h` + `src/io.c`

Versioned binary map persistence:
- Save full map snapshots (tiles + metadata)
- Load full map snapshots
- Format validation with explicit unsupported-format errors

### `rng.h` + `src/rng.c`

Deterministic PRNG wrapper:
- Seed setup
- Integer random helpers
- Range generation helper

### `generator.h` + `src/generator/`

Generation entrypoint and BSP config:
- One algorithm enum value: `DG_ALGORITHM_BSP_TREE`
- One config block: `dg_bsp_config_t`
- BSP controls only:
  - `min_rooms` / `max_rooms`
  - `room_min_size` / `room_max_size`

Internal generator split:
- `src/generator/api.c`: public API validation + orchestration
- `src/generator/bsp.c`: BSP partitioning, room carving, and corridor linking
- `src/generator/primitives.c`: shared geometry/tile helpers
- `src/generator/connectivity.c`: connectivity analysis helpers
- `src/generator/metadata.c`: metadata population and map-state initialization
- `src/generator/internal.h`: internal contracts between generator modules

### `apps/nuklear/core.h` + `apps/nuklear/core.c`

Nuklear-based editor core:
- Owns editor UI state (BSP params, file path, status text)
- Handles generate/save/load via the public API
- Renders map preview and metadata through Nuklear command buffers

### `apps/nuklear/glfw_main.c`

Simple presenter shell:
- GLFW window + OpenGL context
- Nuklear frame lifecycle and rendering
- Calls into Nuklear editor core each frame

## Data flow

1. Create `dg_generate_request_t` (usually via `dg_default_generate_request`).
2. Call `dg_generate`.
3. Read tiles and metadata from `dg_map_t`.
4. Destroy map with `dg_map_destroy`.

## Extensibility direction

- Add new algorithms as dedicated files under `src/generator/`.
- Extend `dg_generate_request_t` with per-algorithm config blocks as new algorithms are introduced.
- Keep metadata schema additive so old maps remain readable.

## Quality gates

- CTest-driven test executable validates API basics and BSP invariants.
- Demo executable provides quick visual verification during iteration.
