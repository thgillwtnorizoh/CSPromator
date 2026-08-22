# Session 005 findings: long-running Casual lifecycle

Session 005 was recorded with CSPromator 0.0.6 and kept the probe running across multiple Casual server/map attachments instead of starting a fresh process for one clean match.

The raw capture is intentionally **not** committed because it contains real player identifiers. Regression fixtures derived from it use synthetic identifiers only.

## Capture summary

```text
snapshots:       502 valid GSI payloads
invalid JSON:    0
duration:        651.034289 s (~10m 51s)
raw.gsi:         894,928 bytes
mode:            casual
QPC frequency:   10,000,000 ticks/sec
```

Observed map lifecycle:

```text
MENU
  -> de_dust2, attached at GAME OVER (CT 1 - 8 T)
  -> MENU
  -> de_vertigo WARMUP
  -> de_vertigo scored match, 0-0 -> 3-0
  -> MENU / leave
  -> de_fachwerk, attached mid-round at CT 2 - 7 T
  -> observe current round, CT reaches 3-7
  -> first local-player state appears in next freeze
  -> final round, T reaches 8 and GAME OVER
  -> MENU
```

This is the first regression session focused on **process lifetime spanning multiple match attachments** rather than one match.

## Lifecycle lessons

### MATCH_ENTERED means attachment, not match start

The first `de_dust2` payload already reported:

```text
map.phase = gameover
round.phase = freezetime
score = CT 1 - 8 T
```

Promator should emit/understand match attachment without inventing a new round or match-start ceremony.

The same distinction matters when joining `de_fachwerk` with a round already live.

### Warmup contains real player events

During `de_vertigo` warmup the local player produced factual kill, death and respawn transitions. The match then switched to `map.phase=live` and local match/round counters reset downward for scored play.

Therefore:

- `PLAYER_KILL`, `PLAYER_DIED` and `PLAYER_RESPAWNED` remain factual in warmup;
- counter resets from warmup to scored play must not manufacture positive events;
- a future Director needs lifecycle context to decide whether a factual event is musically relevant.

0.0.7 exposes lifecycle state instead of creating duplicate event names such as `WARMUP_PLAYER_KILL`.

### Multiple events can happen after round.phase=over

On Vertigo round 1, after the round had already moved to `round.phase=over`, the local round-kill counter increased twice more:

```text
2 -> 3
3 -> 4
```

A local death then arrived while the round was still over.

These remain factual `PLAYER_KILL` / `PLAYER_DIED` events, but post-round context must prevent higher semantic systems from treating them as live-round combat progression. `ROUND_ENDED` must not repeat.

### Match leave must erase match-local knowledge

After leaving Vertigo, the same Promator process later attached to Fachwerk mid-round and initially observed other real players.

The new map must not inherit:

- local-player acquisition/loss state;
- remembered local team or HP;
- round/ace/clutch state;
- supplementary team counts or roster revision;
- any previous map lifecycle context.

The first own-player state on Fachwerk is `LOCAL_PLAYER_ACQUIRED`, not `LOCAL_PLAYER_RESTORED`.

### Casual remains a strong identity stress test

The capture exposed 18 distinct non-local top-level player identities while spectating/attaching. Local numeric state must continue to be accepted only when `player.steamid == provider.steamid`.

## Recorder performance

Even across multiple attachments and rapid state changes, asynchronous persistence remained small compared with GSI receive time.

```text
accepted-packet interval
  minimum:  ~11.427 ms
  median:   ~379.720 ms
  p95:      ~5064.022 ms

accept -> body complete
  median:   ~22.501 ms
  p95:      ~56.507 ms
  maximum:  ~62.582 ms

ACK -> persistence complete
  median:   ~0.140 ms
  p95:      ~0.280 ms
  p99:      ~1.386 ms
  maximum:  ~2.865 ms
```

The persistence worker remains comfortably outside the GSI network critical path.

## 0.0.7 regression targets derived from this session

Session 005 adds regression coverage for:

1. attaching to an already-gameover match without inventing `ROUND_STARTED`;
2. warmup factual kills/deaths/respawns with `scored_round_active=false`;
3. warmup-to-live counter resets not creating fake positive events;
4. live-round lifecycle classification;
5. multiple post-round kills/death remaining factual without duplicate round end or ace semantics;
6. leaving a map clearing match-local state;
7. attaching to a second map mid-round while spectating without false `LOCAL_PLAYER_LOST`/`RESTORED`;
8. first local state on the second map producing `LOCAL_PLAYER_ACQUIRED`;
9. supplementary state from one map never contaminating the next.

## Architectural consequence

Promator now treats lifecycle as context around facts:

```text
PLAYER_KILL
  + lifecycle=warmup     -> factual, normally ignored by Director

PLAYER_KILL
  + lifecycle=live-round -> factual, eligible for Director rules

PLAYER_KILL
  + lifecycle=post-round -> factual, normally excluded from combat escalation
```

This keeps the event vocabulary small while preserving the real behavior observed from CS2.
