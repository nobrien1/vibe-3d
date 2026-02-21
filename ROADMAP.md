# Vibe 3D Roadmap (10 Improvements)

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
