# Vibe 3D Roadmap (10 Improvements)

## Environment beautification pass (implemented)

1. Rolling terrain hills and berms around the play space.
2. Pine tree clusters to establish forested silhouettes.
3. Broadleaf/blossom trees for color variation.
4. Small cabins and utility structures to add landmarks.
5. A raised watchtower to improve skyline readability.
6. Fence lines and ranch-style boundaries near routes.
7. Stone ruins/archway pieces for visual storytelling.
8. Dirt-road style pathing toward major objectives.
9. Flower and shrub patches to soften flat ground.
10. Lantern posts along routes for warm focal points.

### Status

- [x] 1. Rolling hills/berms
- [x] 2. Pine tree clusters
- [x] 3. Broadleaf/blossom trees
- [x] 4. Cabins/structures
- [x] 5. Watchtower landmark
- [x] 6. Fence boundaries
- [x] 7. Stone ruins/archway
- [x] 8. Dirt-road pathing
- [x] 9. Flower/shrub patches
- [x] 10. Lantern posts

## Iteration order

1. Architecture extraction from `main.cpp` into systems (input/simulation/render/audio/network/UI).
2. Fixed-timestep simulation loop with interpolation.
3. Persistent settings + controls profile file.
4. Movement feel polish (buffer/coyote tuning, variable jump cut, landing feedback).
5. Enemy behavior states + telegraphing + adaptive difficulty.
6. Level goal depth (timed medals/checkpoint-style progression metrics).
7. Multiplayer smoothing + packet sequence handling.
8. Performance instrumentation and frame-time graph.
9. Audio mix polish with contextual/spatialized cues.
10. Accessibility and UX polish (rebinds, readability helpers, clear objective text).

## Completion criteria for this iteration

- Ship one concrete, playable improvement for each of the 10 items.
- Keep changes incremental and backward compatible with current save-less flow.
- Keep project single-binary and CMake-compatible.

## Status

- [x] Roadmap created
- [x] 1. Architecture extraction
- [x] 2. Fixed timestep
- [x] 3. Persistent settings/profile
- [x] 4. Movement feel
- [x] 5. Enemy AI states
- [x] 6. Deeper progression goals
- [x] 7. Multiplayer smoothing
- [x] 8. Performance instrumentation
- [x] 9. Audio polish
- [x] 10. Accessibility UX
