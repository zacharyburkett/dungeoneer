# dungeoneer Project Plan

## Vision

Build a standalone C library for high-variety 2D dungeon generation that can be embedded in engine/editor workflows without rewriting core systems.

## Current state (as of reset)

Implemented baseline:
- Room-like generators:
  - BSP Tree
  - Rooms + Mazes
- Cave-like generators:
  - Drunkard's Walk
- Deterministic generation by seed
- Room/corridor metadata and connectivity metrics
- Config-only save/load (load regenerates exact map from saved request)
- Nuklear-based editor app for generate/save/load
- Automated tests for core invariants and config save/load determinism
- Two-stage generation flow:
  - Layout stage (algorithm-specific)
  - Process stage (post-layout transforms)
    - Scale factor
    - Organic room shaping for room-like layouts

## Product direction after pivot

Primary near-term focus is no longer "special hardcoded roles" (entrance/exit/boss/etc).
Primary near-term focus is a **general room-type and constraint system** that works across room-like generators.

## Decision summary: room typing model

Chosen direction:
- Keep generation in two explicit stages:
  1. Layout stage (algorithm-specific): carve tiles + produce room graph.
  2. Process stage (algorithm-agnostic): apply transforms/customization passes.
- Keep room typing as a separate assignment pass after layout/process.
- Make room types user-defined and data-driven (ID + config), not enum-hardcoded behaviors.
- Separate constraints into:
  - Hard constraints: must be satisfied (eligibility + quotas).
  - Soft preferences: weighted scoring when multiple candidates are valid.
- Keep deterministic behavior by making every tie-break seed-driven.
- Manage room-type configs as explicit caller-owned arrays in generation requests (no hidden global registry).

Detailed design contract: `docs/ROOM_TYPES.md`.

## Roadmap

### Phase 1: Spec and API scaffold

- Freeze terminology and config schema for room typing.
- Add new public config model for room-type assignment (non-breaking first pass).
- Complete migration from legacy `dg_room_role_t` to generic room type IDs.
- Keep serialization docs aligned with config-only persistence.

### Phase 2: Room feature extraction

- Build room feature pass from existing metadata/graph:
  - area
  - aspect ratio
  - graph degree
  - border distance
  - path depth / graph distance metrics
- Store computed features in internal generation context.

### Phase 3: Assignment engine

- Implement deterministic assignment pass:
  - eligibility filtering
  - min/max/target quota handling
  - weighted scoring for soft preferences
- Support "fully typed" and "partially typed" modes.
- Define fallback behavior when constraints are infeasible.

### Phase 4: Editor integration

- Add room-type configuration controls in Nuklear app:
  - type list management
  - quota controls
  - constraint sliders/toggles
  - validation messages
- Add map visualization for assigned room types.

### Phase 5: Persistence and compatibility

- Extend map format for room type assignments/config snapshots.
- Keep load path simple and strict for current config format only.
- Add/maintain tests for config roundtrip + deterministic regeneration.

### Phase 6: Quality gates

- Determinism tests for type assignment by seed/config.
- Constraint satisfaction tests (quotas/eligibility rules).
- Property tests for infeasible-config handling.
- GUI smoke test for room-type config lifecycle.

## Definition of done for this milestone

1. Project docs reflect the post-pivot architecture and priorities.
2. Room typing has a clear, implementation-ready design contract.
3. Next coding phase can start from stable API/behavior assumptions.
