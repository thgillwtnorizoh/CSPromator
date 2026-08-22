# Supplementary-state architecture

CSPromator 0.0.7 keeps optional player-visible aggregate state separate from the required GSI factual spine and now gives that supplementary state deterministic session recording/replay before any live Panorama provider exists.

The design remains deliberately asymmetric:

```text
GSI                           supplementary state
(required factual spine)      (optional evidence only)
          \                   /
           \                 /
            EventDetector   TeamCounts
                 \           /
                  SemanticResolver
                        |
                     Director
```

## GSI remains authoritative

Existing GSI responsibilities do not move. GSI continues to own facts such as:

- local kills, headshot kills, assists, damage and death;
- local-player acquisition / spectator identity handling;
- round, freeze, halftime and game-over transitions;
- bomb plant, defuse and explosion state;
- round winner and local win/loss derivation.

Supplementary state must not duplicate those facts merely because another API can expose them.

## Minimal supplementary contract

The first contract contains only aggregate team counts:

```text
CT alive
T alive
CT total
T total
```

No player names, Steam IDs, positions, inventories or hidden enemy state are required.

A `SupplementarySnapshot` also carries:

- an observation tick in the Promator QPC clock domain;
- a session-relative timestamp;
- a source kind;
- an optional `roster_revision`.

A provider increments `roster_revision` whenever team membership changes. Alive-count changes alone do not increment it. This prevents a join/leave sequence such as `5 -> 6 -> 5` from becoming invisible simply because the final numeric total matches the original total.

## Push, do not poll

The live pipeline exposes a second push ingress:

```text
LiveEventPipeline::enqueue_supplementary(...)
```

The core does not contain a Panorama polling loop.

A future provider should observe the minimum necessary values and publish only when useful state changes, or at a deliberately measured low cadence if event-driven observation is not possible.

This keeps Panorama optional and prevents it from becoming a second telemetry firehose.

## Evidence strength and evidence source are separate

0.0.5 introduced evidence strength. 0.0.6 added explicit source provenance.

Examples:

```text
PLAYER_KILL
  strength: deterministic
  source:   gsi

ACE_CANDIDATE
  strength: mode-assumption
  sources:  gsi + mode-rules

ACE
  strength: deterministic
  sources:  gsi + supplementary
```

A supplementary source is not automatically weak evidence. If it deterministically reports a player-visible aggregate value, the semantic result may still be deterministic.

## ACE confirmation

A GSI-only fixed-mode threshold remains `ACE_CANDIDATE`.

The resolver may emit confirmed `ACE` only when all required conditions are satisfied:

1. the round start was observed;
2. a stable enemy-team total was known at the round boundary;
3. a roster revision was available (by default);
4. that revision and enemy total did not change during the round;
5. the local GSI round-kill count equals the initial enemy total;
6. supplementary enemy-alive count reaches zero;
7. the mode is a tested round-based mode (`competitive`, `casual`, or `retakes`).

If roster membership changes, confirmation is disabled for the rest of that round even if the totals later return to their original values.

This permits evidence-backed Casual aces without inventing a fixed Casual kill threshold.

## Clutch semantics

The resolver can derive clutch transitions when:

- GSI proves the local player is alive and the round is live; and
- supplementary state reports exactly one local-team player alive with one or more enemies alive.

Events:

```text
CLUTCH_STARTED value=<enemy alive>
CLUTCH_UPDATED <old>-><new> value=<enemy alive>
CLUTCH_ENDED
```

The semantic context also exposes:

```text
local_alive
enemy_alive
local_total
enemy_total
alive_delta = local_alive - enemy_alive
```

This lets a future Director consume team balance as state rather than forcing an audio event for every numeric fluctuation.

## Lifecycle context

0.0.7 adds match lifecycle beside semantic team state:

```text
detached
warmup
freezetime
live-round
post-round
gameover
other
```

Factual events do not change names across lifecycle phases. For example, warmup and post-round kills remain `PLAYER_KILL`, while a future Director can use:

```text
scored_round_active == true
```

to restrict combat reactions to actual scored live rounds.

Match leave resets lifecycle, local acquisition memory, ace/clutch state and all supplementary knowledge before another map can attach.

## Timing and ordering

Supplementary observations use the same Promator monotonic-clock domain as GSI.

The resolver rejects supplementary snapshots older than the most recently accepted supplementary timestamp. It also rejects a supplementary observation whose observation time is older than GSI state already processed by the resolver. Such a delayed observation may remain in the recording for diagnostics, but it cannot rewind current semantic state.

No production freshness timeout is chosen yet. The configuration supports one, but a real provider must be measured before selecting it.

### Shared ingress order

Schema-v3 sessions assign every queued GSI or supplementary record one monotonically increasing `ingress_order` through a tiny serialized ingress fan-out gate.

```text
order 101  GSI
order 102  supplementary
order 103  supplementary
order 104  GSI
```

The gate runs after the GSI HTTP acknowledgement and performs no JSON parsing or filesystem I/O. It exists only to ensure that persistence and live semantic processing receive cross-source observations in the same accepted order.

`relative_us` remains the observation-time axis. `ingress_order` is the **canonical processing order**. This distinction matters if a provider captures an observation, delays submission, and submits it after newer GSI state. Replay must reproduce the order the live resolver actually saw rather than moving that delayed observation backward in history.

Older v1/v2 sessions contain GSI only, so replay safely synthesizes their ingress order from the original GSI sequence number.

## Schema-v3 recording

New 0.0.7 sessions use:

```text
session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw.gsi
  supplementary.timeline.tsv
```

GSI raw bytes remain unchanged in packed `raw.gsi`.

The supplementary file contains only compact rows:

```text
ingress_order
sequence
observed_tick
persist_complete_tick
relative_us
source
roster_revision
ct_alive
t_alive
ct_total
t_total
```

There is deliberately no giant raw Panorama dump.

The public 0.0.7 CLI still has no live supplementary provider, so ordinary GSI-only recording creates the supplementary file with only its header. The storage API exists now so a future provider cannot be shipped without recording support.

## Replay provenance

A recorded supplementary row stores the provider source that originally produced it, for example `synthetic` or a future `panorama-visible-state`.

When loaded for replay:

```text
snapshot.source = replay
recorded_source = original provider
```

This prevents offline replay from pretending that a live provider is currently connected while still preserving origin metadata for diagnostics.

## Deterministic merged replay

`load_replay_stream()` reconstructs the stream primarily by shared `ingress_order`, because that is the order the live semantic pipeline received. `relative_us` remains the recorded observation time and is used for diagnostics and pacing, but it does not reorder accepted observations.

For example:

```text
order 201  GSI            relative_us=500000
order 202  supplementary  relative_us=490000   <- delayed old observation
```

Replay keeps `201 -> 202`. The resolver then rejects order 202 as stale relative to already-processed GSI, exactly as live processing did.

The `analyze` command runs that reconstructed stream through the same layers used by live interpretation:

```text
recorded GSI ----------------> EventDetector ----+
                                                  |
recorded supplementary ---------------------------+--> SemanticResolver
                                                       |
                                                    semantic output
```

This makes future ACE/clutch decisions reproducible without launching CS2 or the supplementary provider.

## Failure model

No supplementary provider is required.

With no supplementary input:

- GSI recording continues;
- factual events continue;
- `ACE_CANDIDATE` behavior remains;
- confirmed `ACE`, clutch state and aggregate team-balance context remain unavailable rather than guessed.

A future provider failure must degrade back to this exact behavior.

## Panorama boundary

0.0.7 still does **not** implement Panorama access.

A future live provider is acceptable only if it can obtain ordinary player-visible aggregate state through a supported, external, non-invasive mechanism.

Do not use:

- DLL injection;
- process hooks;
- game-memory reads;
- `panorama.dll` patching;
- stock HUD/VPK modification to insert a transmitter;
- hidden-information APIs that expose more than an ordinary player can know.

If no acceptable bridge exists, the provider stays absent. The supplementary architecture remains useful for deterministic tests and any future legitimate source.
