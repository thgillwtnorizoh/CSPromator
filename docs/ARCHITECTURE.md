# CSPromator architecture, Milestone 0

## Purpose

Milestone 0 is a telemetry instrument, not a jukebox. It establishes what modern CS2 actually emits through Game State Integration and when CSPromator receives it.

## Invariants

1. CSPromator stays external to the CS2 process.
2. Raw GSI payloads are preserved before interpretation.
3. Timing-sensitive receive timestamps come from a monotonic high-resolution clock.
4. The HTTP request path performs no audio, waveform, BPM, rule, or UI work.
5. State interpretation will be a later layer. A snapshot is not an event.
6. Missing information is never silently treated as zero/false.
7. Session replay must operate without CS2 running.

## Milestone 0 pipeline

```text
CS2
  |
  | HTTP POST / GSI JSON
  v
GsiHttpServer
  |
  +--> accepted_tick
  +--> body_complete_tick
  |
  v
SessionRecorder
  |
  +--> metadata.json
  +--> timeline.tsv
  +--> raw/00000001.json ...
```

## Clock

On Windows, `MonotonicClock` uses `QueryPerformanceCounter` and records the matching `QueryPerformanceFrequency` in session metadata. The prototype records socket-accept, body-complete, acknowledgement-sent, and persistence-complete timestamps. The HTTP acknowledgement is sent before disk I/O. Neither is falsely labelled as the exact in-engine event time.

## Next layer

After collecting representative CS2 sessions, Milestone 0.2 will add a schema-tolerant normaliser and transition detector:

```text
raw snapshot N-1 + raw snapshot N
             |
             v
      NormalisedGameState
             |
             v
        EventEngine
```

Only after those events are reproducible do we add audio.
