# dungeoneer Project Plan

## Vision

Build a standalone C library for 2D procedural dungeon generation that can later be integrated into a game engine without requiring architectural rewrites.

## Primary goals

1. Provide multiple generation methods with explicit configuration controls.
2. Support custom room behavior and generation constraints.
3. Expose generation metadata useful for gameplay systems.
4. Maintain deterministic output with seed-driven reproducibility.
5. Keep quality high via automated tests and visual demos.

## Scope phases

### Phase 0: Foundation (current)

- Repository structure and CMake build
- Public API skeleton (`include/dungeoneer`)
- Core map and metadata types
- Deterministic RNG
- Two starter algorithms
- Test harness and ASCII demo
- Constraint system baseline:
  - Floor coverage bounds
  - Room/special-room bounds
  - Role-count and graph-distance constraints
  - Per-role weighted room-role placement controls
  - Forbidden regions
  - Retry attempts
- Metadata baseline:
  - Room + corridor records
  - Explicit room adjacency graph
  - Room role tagging and role counts
  - Room-graph metrics (leaf rooms, corridor lengths, entrance-exit distance)
  - Coverage and connectivity diagnostics
  - Effective seed and attempt count

### Phase 1: Generator maturity

- Corridor routing mode controls for rooms+corridors (random/horizontal-first/vertical-first)
- Improve room layout quality and corridor routing options
- Add algorithm-specific validation and diagnostics
- Expand metadata (connectivity graph, entry/exit points, tags)
- Add configurable constraints (no-overlap zones, reserved regions, locks)

### Phase 2: Extensibility

- Introduce plugin-like hooks for custom generation passes
- Add weighted room archetypes and special-room placement rules
- Add post-process filters (doors, dead-end trimming, secret branches)
- Build stable serialization for map + metadata

### Phase 3: Engine integration readiness

- Add strict API versioning and compatibility guarantees
- Produce integration examples for common engine loop patterns
- Benchmark generation performance and memory usage
- Add CI matrix and release packaging

## Testing strategy

- Determinism tests by seed and config
- Structural invariants (bounds, connectivity, outer walls, room validity)
- Regression snapshots for algorithm output drift
- Demo smoke tests in CI

## Definition of done for initial milestone

1. Build and test passes locally with no warnings.
2. Demo executable generates visible dungeons for both algorithms.
3. Baseline docs capture architecture and next implementation phases.
