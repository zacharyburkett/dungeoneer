# dungeoneer

`dungeoneer` is a standalone C library for generating 2D procedural dungeons. It is designed to stay engine-agnostic while still being straightforward to embed into a game engine later.

Current baseline includes:
- A C11 library target with a small, focused API
- Two generation strategies:
  - Rooms + corridors
  - Organic cave carving
  - Corridor routing controls for rooms (`random`, `horizontal-first`, `vertical-first`)
- Rich map metadata:
  - Room + corridor metadata
  - Room role tagging (entrance/exit/boss/treasure/shop)
  - Explicit room adjacency graph (neighbors per room)
  - Room-graph metrics (leaf rooms, total corridor length, entrance-exit distance)
  - Tile coverage counts
  - Connectivity diagnostics
  - Generation attempts and effective seed
- Constraint controls:
  - Connectivity and outer walls
  - Min/max floor coverage
  - Min/max room count and min special-room count
  - Required role counts, entrance-exit distance, boss-on-leaf rule
  - Per-role placement weights (distance/degree/leaf bonus)
  - Forbidden (no-carve) regions
  - Retry budget for constraint satisfaction
- Algorithm config validation:
  - Invalid per-algorithm parameters fail fast with `DG_STATUS_INVALID_ARGUMENT`
- Deterministic RNG (seed-based generation)
- Unit tests and an ASCII visualization demo

## Build

```sh
cmake -S . -B build
cmake --build build
```

To build the optional Nuklear app + GLFW presenter:

```sh
cmake -S . -B build -DDUNGEONEER_BUILD_NUKLEAR_APP=ON
cmake --build build
```

By default (`DUNGEONEER_NUKLEAR_AUTO_FETCH=ON`), CMake auto-fetches Nuklear and GLFW
via `FetchContent` during configure (network access required), then builds
`dungeoneer_editor`.

For offline or fully pinned dependencies, disable auto-fetch:

```sh
cmake -S . -B build -DDUNGEONEER_BUILD_NUKLEAR_APP=ON -DDUNGEONEER_NUKLEAR_AUTO_FETCH=OFF
cmake --build build
```

With auto-fetch off, CMake looks for `nuklear.h` and `nuklear_glfw_gl2.h` in
`third_party/`, `/opt/homebrew/include`, and `/usr/local/include`, and expects
GLFW + OpenGL to be available on the system.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Demo

```sh
./build/dungeoneer_ascii_demo rooms 80 40 42
./build/dungeoneer_ascii_demo organic 80 40 42
./build/dungeoneer_ascii_demo rooms 80 40 42 horizontal-first
./build/dungeoneer_ascii_demo rooms 80 40 42 vertical-first
# if built with -DDUNGEONEER_BUILD_NUKLEAR_APP=ON
./build/dungeoneer_editor
```

The demo also prints metadata diagnostics (rooms, corridors, coverage, connectivity, attempts).

## Map Serialization

Binary map snapshots can be saved and loaded through:
- `dg_map_save_file(const dg_map_t *map, const char *path)`
- `dg_map_load_file(const char *path, dg_map_t *out_map)`

The format is versioned and includes tiles plus runtime metadata, which makes it suitable for editor/GUI workflows (generate, save, reload, inspect).

## Role placement tuning

Role assignment can be tuned per role with weighted scoring:

`score = distance_weight * distance_from_entrance + degree_weight * room_degree + leaf_bonus_if_leaf`

`dg_default_generate_request` initializes sensible defaults for each role. When both entrance and exit are required, the first pair is still selected by maximum room-graph distance before weighted selection is used for additional entrance/exit slots.

## Project docs

- Project roadmap: `docs/PROJECT_PLAN.md`
- Technical layout: `docs/ARCHITECTURE.md`

## Integration notes

- The generation API writes to `dg_map_t`.
- `dg_map_t` should be zero-initialized before first use.
- Call `dg_map_destroy` after you are done with a generated map.
