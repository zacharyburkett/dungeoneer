# Architecture Overview

## Repository layout

- `include/dungeoneer/`: Public API headers
- `src/`: Library implementation
- `tests/`: Test executable source
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
- Generation-class tagging (`room-like` / `cave-like`)
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

Generation entrypoint and algorithm configs:
- `DG_ALGORITHM_BSP_TREE`
- `DG_ALGORITHM_DRUNKARDS_WALK`
- `DG_ALGORITHM_ROOMS_AND_MAZES`
- Algorithms map into two generation classes:
  - Room-like (BSP, Rooms + Mazes): produces room/corridor metadata
  - Cave-like (Drunkard's Walk): focuses on tile topology without room graph data
- Config blocks:
  - `dg_bsp_config_t` (`min_rooms`, `max_rooms`, `room_min_size`, `room_max_size`)
  - `dg_drunkards_walk_config_t` (`wiggle_percent`)
  - `dg_rooms_and_mazes_config_t` (`min_rooms`, `max_rooms`, `room_min_size`, `room_max_size`, `maze_wiggle_percent`, `min_room_connections`, `max_room_connections`, `ensure_full_connectivity`, `dead_end_prune_steps`)

Internal generator split:
- `src/generator/api.c`: public API validation + orchestration
- `src/generator/bsp.c`: BSP partitioning, room carving, and corridor linking
- `src/generator/drunkards_walk.c`: single-walker cave carving with wiggle control
- `src/generator/rooms_and_mazes.c`: random room placement + maze carving + connector + dead-end pruning
- `src/generator/primitives.c`: shared geometry/tile helpers
- `src/generator/connectivity.c`: connectivity analysis helpers
- `src/generator/metadata.c`: class-aware metadata population and map-state initialization
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
- Nuklear editor build acts as UI smoke coverage during iteration.
