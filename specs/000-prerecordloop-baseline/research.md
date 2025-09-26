# Research: Pre-Record Loop Baseline (Phase 0)

## Decisions
- **Time-only capacity**: Use integer seconds (`max-time`) with adaptive two-GOP floor.
  - Rationale: Simplifies configuration; ensures at least meaningful pre-roll.
  - Alternatives: buffer/byte limits (dropped for simplicity / YAGNI).
- **Adaptive floor (2 GOPs)**: Prevents flush producing unusable fragment.
  - Alternatives: strict cutoff (risk of zero keyframe), partial trimming (decoder corruption risk).
- **Manual re-arm via `prerecord-arm`**: Gives application-level control; avoids hidden re-buffering.
  - Alternatives: automatic timer or GOP-count re-arm; rejected as surprising.
- **AUTO `flush-on-eos`**: Privacy & intent alignment—only flush if context already revealed by prior flush.
  - Alternatives: always flush (could leak pre-event data), never flush (loses expected trailing content for long recordings).
- **Integer second granularity**: Reduces churn in prune logic and timestamp rounding complexity.
  - Alternatives: nanosecond precision (complexity, jitter), frame-based (requires knowing/handling variable framerates).

## Open (Deferred) Topics
- Performance benchmarks: define target throughput & memory for 4K30 vs 1080p60.
- Metrics instrumentation design (GObject properties vs structured logging counters).
- Seek handling complexity (current assumption: passthrough; advanced random access deferred).
- Multi-threaded test harness vs single-thread simulation (decide when adding perf tests).

## Alternatives Considered
| Topic | Alternative | Reason Rejected |
|-------|------------|-----------------|
| Capacity | bytes / buffers | Increases config surface without immediate need |
| Oversized GOP | truncate GOP | Produces undecodable stream start |
| Re-arm | automatic timer | Hidden state changes; less deterministic |
| flush-on-eos default | always flush | Potential privacy/incomplete event leak |
| granularity | nanoseconds | Adds complexity; marginal user value |

## Risks & Mitigations
| Risk | Impact | Mitigation |
|------|--------|-----------|
| Large GOP spans > limit | Pre-roll overshoot | Adaptive floor design |
| Missing keyframe before flush | Corrupt start | Enforce start-at-keyframe rule |
| Memory growth under burst | Elevated RSS | Early pruning after third GOP arrival |
| Concurrent flush events | Race / duplicate output | Ignore subsequent during drain |
| Lack of observability | Hard to tune pruning | Add counters in Phase implementation |

## Glossary
- **Adaptive Floor**: Policy guaranteeing at least two complete GOPs whenever possible.
- **Re-Arm**: Action of returning element to buffering mode after a flush via `prerecord-arm`.

## Completion
All clarifications resolved; no blocking unknowns for Phase 1.
