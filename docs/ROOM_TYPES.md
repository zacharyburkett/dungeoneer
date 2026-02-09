# Room Types and Configuration

## Goal

Define a general, deterministic, and extensible system for assigning semantic room types to room-like dungeon layouts.

This system should:
- Work across multiple room-like algorithms.
- Avoid hardcoded game-specific role assumptions.
- Allow strict constraints and softer style preferences.
- Remain deterministic by seed.

## Scope boundaries

In scope:
- Assigning type IDs to generated rooms.
- Validating and applying type constraints.
- Producing metadata needed by downstream gameplay/editor tooling.

Out of scope (for first implementation):
- Dynamic runtime scripting.
- Arbitrary user-defined expression languages.
- Tile-level decoration/content placement.

## Terminology

- Room type: A semantic label represented by a stable numeric ID.
- Eligibility: Hard checks determining whether a room can be assigned a given type.
- Quotas: Min/max/target counts per type.
- Preferences: Weighted scoring hints among eligible candidates.

## v1 concrete decisions

- Room type IDs are arbitrary `uint32_t` values chosen by the caller and must be unique within a request.
- Public configuration is provided as caller-owned arrays (`pointer + count`), not a global mutable registry.
- Solver internals remap type IDs into contiguous working indices for efficient assignment.
- Room-type config is algorithm-agnostic and shared across all room-like generators.
- Strict mode fails the current generation attempt immediately when constraints are infeasible.
- Automatic retry-on-failure is deferred to higher-level orchestration (editor/app), not embedded in v1 solver logic.

## Proposed configuration model

### Type definition (conceptual)

Each room type defines:
- `type_id` (uint32): stable identifier.
- `name` (optional UI label, not required for runtime logic).
- `enabled` (bool).
- `min_count` / `max_count` / `target_count` (quota controls).
- `weight` (default selection bias).
- Eligibility constraints:
  - `area_min`, `area_max`
  - `degree_min`, `degree_max`
  - `border_distance_min`, `border_distance_max`
  - optional graph-depth window from layout anchor/root
- Preference knobs (soft scores):
  - prefer larger/smaller rooms
  - prefer hub-like (high degree) or leaf-like (low degree)
  - prefer near-border or central placement

### Global assignment policy

Single policy block per request:
- `strict_mode`:
  - `1`: fail generation when hard constraints cannot be satisfied.
  - `0`: best effort, with diagnostics in metadata.
- `allow_untyped_rooms`:
  - `1`: rooms may remain default type when no valid assignment.
  - `0`: all rooms must receive some configured type.
- `default_type_id`:
  - fallback type for unspecified rooms.

## Assignment algorithm (deterministic)

For room-like maps:
1. Compute room feature table from geometry + adjacency graph.
2. Build eligibility matrix (room x type) from hard constraints.
3. Satisfy mandatory minima first (`min_count`) using deterministic candidate order + weighted tie break.
4. Fill remaining rooms using weighted preference scoring and quota caps.
5. Apply fallback/untyped policy.
6. Emit diagnostics:
  - unmet minima
  - capped maxima
  - count summary by type

Determinism contract:
- Candidate ordering is stable.
- Any random tie-break uses request seed through `dg_rng_t`.

## Failure behavior

If constraints are infeasible:
- Strict mode: return generation failure with constraint error code (to be introduced).
- Best effort mode: return map + diagnostics metadata indicating violations.

## Metadata and serialization plan

### Metadata additions (planned)

Add room-type-specific metadata:
- Per-room assigned `type_id`.
- Aggregate counts by type.
- Assignment diagnostics summary.

### Backward compatibility

- File format remains versioned and additive.
- Older map versions load with default/untyped assignment.
- Legacy `dg_room_role_t` is considered transitional and should map into room type IDs internally until removal.

## UI/editor model

Editor should expose:
- Type list table (ID, enabled, quota, weight).
- Eligibility controls per type.
- Global strict/best-effort toggle.
- Clear validation messages for impossible configurations.

## Implementation rollout

1. Add API structs and defaults for room-type config (no-op when omitted).
2. Add room feature extraction pass.
3. Add assignment pass with deterministic behavior.
4. Add metadata + serialization updates (version bump).
5. Add GUI controls and diagnostics.
6. Add test coverage:
  - determinism
  - quota satisfaction
  - infeasible strict-mode failure
  - best-effort diagnostics

## Deferred questions (not blocking v1)

- Should we add optional per-algorithm overrides for type eligibility in v2?
- Should config snapshots include human-readable labels or only stable IDs?
