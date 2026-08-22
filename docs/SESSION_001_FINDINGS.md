# Session 001 findings

First real CSPromator capture: four bot competitive rounds on `de_inferno`, recorded on 2026-08-22 with probe 0.0.1.

The raw user capture is intentionally **not committed** because GSI includes account/player identifiers. Regression tests use anonymized minimal snapshots derived from the observed transitions.

## Confirmed active-player behavior

- Top-level `player` is the local player while `player.steamid == provider.steamid`.
- After local death, top-level `player` follows the currently observed/spectated player. Local counters must not be compared across that identity switch.
- Requested spectator-rich structures (`allplayers`, detailed root `bomb`, `phase_countdowns`, positions) were absent while actively playing.
- Local health, round kills, round headshot kills, match stats, team, map, round phase, coarse bomb state, halftime and game-over state were usable.
- Multiple facts can change in one GSI snapshot. One observed packet contained fifth kill + fifth headshot + MVP + round end + halftime + bomb clear.
- The final round transitioned from `round.phase=live` directly to `round.phase=freezetime` while `map.phase=gameover`; an event engine that requires `live -> over` misses the final round end.
- `provider.timestamp` is integer-second precision and is metadata, not Promator's scheduling clock.
- No usable local movement/position stream was present, so `PLAYER_FIRST_MOVEMENT` is not a core-GSI event.

## Probe problem exposed by the session

Probe 0.0.1 acknowledged GSI before disk I/O, but then persisted synchronously before returning to `accept()`. A persistence stall around 1.19 seconds delayed the next socket acceptance, contaminating the supposedly earliest receive timestamp.

Probe 0.0.2 therefore moves persistence to a worker thread:

```text
network thread
  accept -> timestamp -> receive -> ACK -> enqueue -> immediately accept again
                                      |
                                      v
                              persistence worker
```

New sessions also use one packed `raw.gsi` stream plus offsets in `timeline.tsv`, instead of one filesystem allocation per snapshot. Replay remains compatible with v1 session folders.

## Regression cases created

1. Local death followed by spectator target switch must not create fake negative kills/healing.
2. Ace + round end + halftime in one packet must remain one event batch containing all facts.
3. Halftime T -> CT swap must produce team/intermission transitions.
4. Final `live -> freezetime + gameover` must still produce exactly one round end and must not emit a fake next-round freeze.
