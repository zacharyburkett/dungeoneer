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

Algorithms map into two classes:
- Room-like: BSP, Rooms + Mazes
- Cave-like: Drunkard's Walk

Current config blocks:
- `dg_bsp_config_t`
- `dg_drunkards_walk_config_t`
- `dg_rooms_and_mazes_config_t`
- `dg_process_config_t` (post-layout transforms)

Internal generator split:
- `src/generator/api.c`: public API validation + orchestration
- `src/generator/bsp.c`: BSP room and corridor generation
- `src/generator/drunkards_walk.c`: cave carving by random walk
- `src/generator/rooms_and_mazes.c`: room placement + maze carving + connectors + pruning
- `src/generator/process.c`: post-layout transforms (scaling, room shaping)
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
3. Process transforms (algorithm-agnostic post passes)
4. Room graph/feature extraction (room-like only)
5. Room type assignment (algorithm-agnostic)
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
- Persist generation config only (no tile/metadata snapshot), then regenerate maps on load.
- Keep strict-mode failure local to a single generation attempt; retries are handled by callers.

## Extensibility direction

- Add new layout algorithms as dedicated files under `src/generator/`.
- Keep room-typing logic shared and independent from any specific layout algorithm.
- Keep metadata schema focused on runtime diagnostics; persistence is config-driven.

## Quality gates

- CTest-driven executable validates API and generation invariants.
- Serialization tests validate format compatibility and roundtrip integrity.
- Editor build and runtime smoke checks cover UI integration paths.
