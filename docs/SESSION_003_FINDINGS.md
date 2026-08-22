# Session 003 findings

Third real CSPromator capture: Retakes on `de_dust2`, recorded on 2026-08-22 with probe 0.0.3.

The raw user capture is intentionally **not committed** because GSI contains account/player identifiers. Regression tests use anonymized minimal snapshots derived from observed transitions.

## Match shape

- `map.mode` was `retakes`.
- The capture contains warmup plus eight scored live rounds; the final score is CT 8 - T 0.
- Every scored round followed the Retakes post-plant lifecycle:

```text
freezetime -> live -> bomb=planted -> bomb=defused / round end
```

- The transition from `ROUND_STARTED` to `bomb=planted` arrived between roughly 0.03 and 1.34 seconds later across the eight rounds.
- The final round again skipped an intermediate `round.phase=over` and moved to `map.phase=gameover` with `round.phase=freezetime`.

## New proven local-state signals

- `player.state.smoked` produced three live-round rising edges followed by decay.
- `player.state.burning` produced two live-round rising edges, beginning near 255 and decaying toward zero.
- `player.state.flashed` continued to behave as a useful rising-edge signal.

0.0.4 therefore adds `PLAYER_SMOKED` and `PLAYER_BURNING` as rising-edge events. The numeric values are retained as observed GSI state, not interpreted as percentages.

## Bomb state is richer in Retakes

Unlike earlier captures where the planted state mostly disappeared at round end, Retakes repeatedly reported:

```text
bomb: planted -> defused
```

0.0.4 adds explicit `BOMB_DEFUSED` while retaining the generic `BOMB_STATE_CLEARED` transition for compatibility and lower-level rules.

The event name describes the observed bomb state transition. It does not imply that CSPromator knows which player performed the defuse.

## Retakes-specific caution: do not invent aces

Several rounds reached `round_kills=3`, but active-player GSI still did not provide the full opponent roster needed to prove that three kills eliminated every enemy in a Retakes round.

The existing `ACE` heuristic therefore remains restricted to modes where the current five-kill rule is explicitly supported. CSPromator must not redefine `3 kills in retakes` as an ace merely because the usual Retakes team size often makes that plausible.

## Post-round kills are real

One local kill/headshot increment arrived while `round.phase=over`, several seconds after the bomb had already been reported defused and the round winner was known.

This is valid telemetry and remains a `PLAYER_KILL`, but future director rules must be able to condition on the accompanying normalized round phase if they want to suppress post-round music responses.

## Mode-aware director implication

Retakes should not require a special fork of the telemetry engine. `map.mode=retakes` is context carried alongside the same generic events.

A future director can express behavior such as:

```text
WHEN BOMB_PLANTED
IF map.mode == retakes
THEN enter RETAKE_POST_PLANT soundtrack state
```

rather than creating a separate Retakes-only GSI pipeline.
