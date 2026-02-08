# dungeoneer

`dungeoneer` is a standalone C library for generating 2D procedural dungeons. It is designed to stay engine-agnostic while still being straightforward to embed into a game engine later.

Current baseline includes:
- A C11 library target with a small, focused API
- Two generation strategies:
  - Rooms + corridors
  - Organic cave carving
- Rich map metadata:
  - Room + corridor metadata
  - Explicit room adjacency graph (neighbors per room)
  - Room-graph metrics (leaf rooms, total corridor length)
  - Tile coverage counts
  - Connectivity diagnostics
  - Generation attempts and effective seed
- Constraint controls:
  - Connectivity and outer walls
  - Min/max floor coverage
  - Min/max room count and min special-room count
  - Forbidden (no-carve) regions
  - Retry budget for constraint satisfaction
- Deterministic RNG (seed-based generation)
- Unit tests and an ASCII visualization demo

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Demo

```sh
./build/dungeoneer_ascii_demo rooms 80 40 42
./build/dungeoneer_ascii_demo organic 80 40 42
```

The demo also prints metadata diagnostics (rooms, corridors, coverage, connectivity, attempts).

## Project docs

- Project roadmap: `docs/PROJECT_PLAN.md`
- Technical layout: `docs/ARCHITECTURE.md`

## Integration notes

- The generation API writes to `dg_map_t`.
- `dg_map_t` should be zero-initialized before first use.
- Call `dg_map_destroy` after you are done with a generated map.
