# First step: establish the CS2 telemetry truth

CSPromator does **not** begin with audio. It begins by measuring the input that every future audio decision depends on.

## Question Milestone 0 answers

> What information does current CS2 actually send to an actively playing external GSI client, how is it grouped, and when does it reach us?

That question must be answered with captured evidence, not inherited assumptions from old CSGO-era software.

## Prototype contract

The first prototype has exactly five responsibilities:

1. Listen on loopback for GSI HTTP POSTs.
2. Timestamp arrival with a monotonic high-resolution clock.
3. Return HTTP 200 before disk I/O or game-state interpretation.
4. Preserve the raw JSON body byte-for-byte in a session folder.
5. Replay the captured timing later without CS2.

It explicitly does **not**:

- play audio;
- infer kills, clutches, movement, or combat;
- use UI timers as a clock;
- parse old CS-Jukebox model classes;
- assume spectator-only fields exist while playing;
- read or alter the CS2 process.

## Timing points recorded

For every GSI POST the probe stores:

- `accepted_tick`: local TCP connection accepted;
- `body_complete_tick`: complete HTTP request body available in memory;
- `ack_sent_tick`: acknowledgement sent back to CS2;
- `persist_complete_tick`: raw snapshot finished writing to disk;
- `relative_us`: body-complete time relative to session start.

This lets us separate network/input cost from persistence cost instead of calling all of it "GSI latency".

## Exit criteria

Milestone 0 is complete when we have representative active-player sessions and can answer, with examples, which fields reliably support each desired director event. Replaying the same session must produce the same snapshot sequence and timing schedule every time.

Only then do we implement `NormalisedGameState` and the transition/event engine.
