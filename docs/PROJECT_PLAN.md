# dungeoneer Project Plan

## Vision

Build a standalone C library for 2D procedural dungeon generation that can later be integrated into a game engine without architectural rewrites.

## Current reset direction

The generation stack has been intentionally reset to a clean baseline.

Current generator scope is intentionally narrow:
- One algorithm: vanilla BSP tree dungeon generation
- One config family: min/max room count and min/max room size
- Deterministic output by seed
- Solid tests and editor/demo tooling

This baseline is meant to provide a stable foundation before adding advanced controls.

## Scope phases

### Phase 0: Baseline BSP (current)

- Repository structure and CMake build
- Public generation API reduced to BSP essentials
- BSP room/corridor generation implementation
- Runtime metadata population
- Binary map serialization (save/load)
- Nuklear editor for generate/save/load with BSP controls
- Test harness and ASCII demo updated to BSP baseline

### Phase 1: BSP quality and diagnostics

- Improve BSP split heuristics for better room distributions
- Add corridor carving variations while preserving simple controls
- Add regression snapshots for deterministic output drift
- Expand metadata diagnostics around layout quality

### Phase 2: New algorithm expansion

- Introduce second generator family with its own config block
- Keep algorithm configs isolated and composable
- Add cross-algorithm invariants and comparison tests

### Phase 3: Engine integration readiness

- API versioning and compatibility guidelines
- Integration examples for game/editor loops
- Performance benchmarking and profiling
- CI/release packaging hardening

## Testing strategy

- Determinism tests by seed and BSP config
- Structural invariants (bounds, connectivity, outer walls, room validity)
- Serialization roundtrip coverage
- Demo/editor smoke checks in CI

## Definition of done for baseline milestone

1. Build and tests pass locally with strict warnings enabled.
2. BSP demo/editor both generate valid maps.
3. Docs reflect the reset architecture and next phases.
