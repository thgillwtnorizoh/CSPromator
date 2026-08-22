# Session 004 findings

Fourth real CSPromator capture: a human Casual match joined in progress at CT 5 - T 3, recorded on 2026-08-22 with probe 0.0.4.

The raw user capture is intentionally **not committed** because GSI contains account/player identifiers. Regression tests use anonymized minimal snapshots derived from observed transitions.

## Match attachment is not match start

The first usable map snapshot already described an active Casual round at 5-3. The local player's own top-level `player` state was not yet available; CS2 instead supplied the players being spectated until the next round.

Therefore:

```text
MATCH_ENTERED
```

means CSPromator attached to a match, not that the match itself just started.

0.0.4 incorrectly interpreted the first spectated player as `LOCAL_PLAYER_LOST` and the first later local state as `LOCAL_PLAYER_RESTORED`.

0.0.5 introduces:

```text
LOCAL_PLAYER_ACQUIRED
```

and does not permit `LOCAL_PLAYER_LOST` until local state has actually been acquired at least once. Real death/spectator cycles after acquisition continue to use `LOCAL_PLAYER_LOST`, `LOCAL_PLAYER_RESTORED`, and `PLAYER_RESPAWNED`.

## Casual roster uncertainty

The session contained real human players, spectator-target changes and a match joined in progress, but normal active-player GSI still exposed no authoritative global roster, per-team player count, per-team alive count, join event, leave event or kick event.

This makes a fixed Casual kill threshold insufficient evidence for an ace. A factual event such as:

```text
PLAYER_KILL value=5
```

is still valid, but 0.0.5 no longer converts five Casual kills into `ACE` or `ACE_CANDIDATE`.

## ACE semantic revision

GSI-only ace detection is now evidence-sensitive:

- Competitive crossing five round kills produces `ACE_CANDIDATE` with `mode-assumption` evidence.
- Retakes uses the mode/team shape as a candidate threshold: CT 3, T 4, also with `mode-assumption` evidence.
- Casual produces no ace candidate from kill count alone.
- `ACE` is reserved for a future source that can deterministically establish the relevant opposing roster and prove that the local player personally eliminated it.

A candidate is only generated for a kill belonging to a live round. A late post-round kill cannot manufacture an ace candidate.

## Explicit bomb explosion

The Casual capture provided a clean:

```text
bomb: planted -> exploded
```

transition. 0.0.5 adds:

```text
BOMB_EXPLODED
```

while retaining the generic `BOMB_STATE_CLEARED` event for every planted-state exit.

This mirrors the existing explicit `BOMB_DEFUSED` plus generic clear behavior.

## Director implications

Session 004 reinforces several separation rules:

1. Match attachment context must be distinct from a true match-start concept.
2. Local-player availability is stateful and cannot be inferred from whichever top-level `player` happens to be present.
3. Factual kill counters must remain separate from achievement semantics such as ace.
4. Events such as repeated smoke entries are valid raw semantic events; cooldown/coalescing belongs in the future director/arbitration layer, not in the telemetry detector.
5. Missing roster information remains `UNKNOWN` rather than being guessed.

These semantics are intentionally cleaned up before investigating Panorama as a possible supplementary source for small pieces of player-visible state that GSI does not export.
