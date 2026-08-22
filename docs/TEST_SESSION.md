# First controlled telemetry session

Run the probe and then perform a short controlled CS2 session. The goal is observation, not assumptions.

Record these transitions where practical:

1. Main menu to a practice or normal match.
2. Freeze time begins.
3. Freeze time ends while the player remains still briefly.
4. Player starts moving.
5. Player stops and moves again.
6. Local player health changes.
7. Local player's round-kill counter changes.
8. Bomb state changes if naturally encountered in the session.
9. Local player dies or the round otherwise ends.
10. Next freeze time begins.
11. Return to menu.

For each desired future event we will inspect:

- which JSON field(s) changed;
- whether the data was available while actively playing rather than spectating;
- receive cadence and jitter;
- whether CS2 grouped multiple changes into one payload;
- whether the signal is direct state or must be derived;
- whether any observable delay is consistent.

Do not infer unavailable opponent information. CSPromator is an audio companion, not a gameplay-information tool.
