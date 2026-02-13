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
- Room and corridor metadata
- Room adjacency graph metadata (spans + neighbor list)
- Runtime diagnostics (coverage/connectivity/attempts)

### `io.h` + `src/io.c`

Persistence and export:
- Save/load generation configuration snapshots (`.dgmap`) for deterministic regeneration
- PNG + JSON export for engine-agnostic consumption
- Snapshot validation and strict schema checks

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
- `DG_ALGORITHM_ROOM_GRAPH`
- `DG_ALGORITHM_WORM_CAVES`
- `DG_ALGORITHM_CELLULAR_AUTOMATA`
- `DG_ALGORITHM_VALUE_NOISE`
- `DG_ALGORITHM_SIMPLEX_NOISE`

Algorithms map into two classes:
- Room-like: BSP, Rooms + Mazes, Room Graph
- Cave-like: Drunkard's Walk, Worm Caves, Cellular Automata, Value Noise, Simplex Noise

Current config blocks:
- `dg_bsp_config_t`
- `dg_drunkards_walk_config_t`
- `dg_rooms_and_mazes_config_t`
- `dg_room_graph_config_t`
- `dg_worm_caves_config_t`
- `dg_cellular_automata_config_t`
- `dg_value_noise_config_t`
- `dg_simplex_noise_config_t`
- `dg_process_config_t` (post-layout transforms)

Internal generator split:
- `src/generator/api.c`: generation orchestration only
- `src/generator/request_validation.c`: request/config validation
- `src/generator/request_snapshot.c`: request-to-metadata snapshot capture
- `src/generator/defaults.c`: default config and request builders
- `src/generator/bsp.c`: BSP room and corridor generation
- `src/generator/drunkards_walk.c`: cave carving by random walk
- `src/generator/room_graph_mst.c`: room packing + MST/loop graph corridors
- `src/generator/worm_caves.c`: multi-agent cave digging
- `src/generator/cellular_automata.c`: cellular cave generation
- `src/generator/value_noise.c`: value-noise cave generation
- `src/generator/simplex_noise.c`: simplex-noise cave generation
- `src/generator/rooms_and_mazes.c`: room placement + maze carving + connectors + pruning
- `src/generator/process.c`: post-layout transforms (scaling, room shaping, path smoothing, corridor roughening)
- `src/generator/room_types.c`: room type assignment and constraints
- `src/generator/primitives.c`: shared geometry/tile helpers
- `src/generator/connectivity.c`: connectivity analysis helpers
- `src/generator/metadata.c`: class-aware metadata population and map-state initialization
- `src/generator/internal.h`: internal contracts between generator modules

### `apps/nuklear/core.h` + `apps/nuklear/core.c`

Nuklear editor core:
- Owns editor UI state
- Handles generate/save/load through public API
- Renders map preview and metadata

### `apps/nuklear/glfw_main.c`

Simple presenter shell:
- GLFW window + OpenGL context
- Nuklear frame lifecycle and rendering
- Calls into Nuklear editor core each frame

## Generation pipeline (target shape)

Pipeline is being standardized as:
1. Input validation + seed setup
2. Layout generation (algorithm-specific)
3. Room feature extraction / metadata bootstrap
4. Room type assignment (room-like only)
5. Process transforms (algorithm-agnostic post passes)
6. Metadata finalize + return

This keeps algorithm code focused on geometry while shared logic handles room semantics.

## Room-type configuration architecture (planned)

Configuration is split by concern:
- Layout config:
  - Algorithm-specific controls (already present in `dg_generate_request_t`).
- Typing config:
  - Generator-agnostic room-type definitions, quotas, and constraints.
  - Passed as caller-owned arrays (`pointer + count`) for deterministic, explicit ownership.
- Policy config:
  - Global behavior for infeasible constraints (strict fail vs best effort).

Data model direction:
- Move from hardcoded role semantics toward user-defined room type IDs.
- Preserve deterministic assignment with seed-driven tie breaking.
- Persist generation config only, then regenerate maps on load.
- Keep strict-mode failure local to a single generation attempt; retries are handled by callers.

## Extensibility direction

- Add new layout algorithms as dedicated files under `src/generator/`.
- Keep room-typing logic shared and independent from any specific layout algorithm.
- Keep metadata schema focused on runtime diagnostics; persistence is config-driven.

## Quality gates

- CTest-driven executable validates API and generation invariants.
- Serialization tests validate format compatibility and roundtrip integrity.
- Editor build and runtime smoke checks cover UI integration paths.
