# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

The project is deliberately building the telemetry spine before the audio machinery. Prototype **0.0.5** records current CS2 Game State Integration traffic, interprets it live, and now distinguishes factual telemetry from higher-level claims that GSI cannot fully prove.

No audio engine is included yet. That is intentional.

## Prototype 0.0.5

The probe now:

- listens only on `127.0.0.1`;
- timestamps receive activity using `QueryPerformanceCounter` on Windows;
- acknowledges GSI before persistence, JSON parsing, event detection, or director work;
- fans each payload into independent persistence and live-interpretation worker queues;
- stores raw payload bytes in one packed `raw.gsi` stream;
- replays both 0.0.1 per-file sessions and modern packed sessions;
- accepts local-player numeric state only when `player.steamid == provider.steamid`;
- distinguishes first `LOCAL_PLAYER_ACQUIRED` from later `LOCAL_PLAYER_LOST` / `LOCAL_PLAYER_RESTORED` spectator cycles;
- derives round/freeze transitions, win/loss, damage/death/respawn, kills/headshots, assists, flash/smoke/burning rising edges, MVP, bomb plant/defuse/explosion, halftime, team swap and game over;
- carries an event-evidence field so non-deterministic semantics can disclose their basis;
- emits `ACE_CANDIDATE evidence=mode-assumption` for supported fixed-mode thresholds instead of asserting a GSI-only `ACE`;
- emits no Casual ace candidate from kill count alone because Casual roster membership can change during a round;
- keeps Retakes and Casual as mode context instead of forking the telemetry engine into mode-specific pipelines;
- prints derived event batches live during `record`;
- exposes `analyze` to run the exact same event detector over old sessions;
- includes anonymized regressions from four real test sessions.

See:

- [`docs/SESSION_001_FINDINGS.md`](docs/SESSION_001_FINDINGS.md)
- [`docs/SESSION_002_FINDINGS.md`](docs/SESSION_002_FINDINGS.md)
- [`docs/SESSION_003_FINDINGS.md`](docs/SESSION_003_FINDINGS.md)
- [`docs/SESSION_004_FINDINGS.md`](docs/SESSION_004_FINDINGS.md)

## Get the Windows build without a local toolchain

GitHub Actions is the default build path. Every push to `main` builds Windows x64/MSVC, runs all regression tests plus the QPC smoke test, and uploads a portable artifact.

1. Open **Actions**.
2. Open the latest successful **Windows Build**.
3. Download `CSPromator-0.0.5-windows-x64`.
4. Extract the artifact and run the probe directly.

No Visual Studio or CMake installation is required on the target machine.

### Optional local build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Install the GSI configuration

Copy:

```text
config\gamestate_integration_cspromator.cfg
```

to:

```text
<Counter-Strike Global Offensive>\game\csgo\cfg\
```

or run:

```powershell
.\install-gsi.ps1 -Cs2GameRoot "C:\...\steamapps\common\Counter-Strike Global Offensive\game"
```

Restart CS2 if it was already open.

## Record + live events

```powershell
.\cspromator-probe.exe record 3010 sessions
```

Example:

```text
[GSI] #42 t+37120.4 ms bytes=2190 ingress=31.8 ms ack=31.9 ms queued
[LIVE] #42 t+37120.4 ms
  PLAYER_KILL amount=1 value=5
  ACE_CANDIDATE value=5 evidence=mode-assumption
```

`ACE_CANDIDATE` is deliberately not the same event as `ACE`. The candidate says that a supported mode rule was satisfied; confirmed `ACE` is reserved for future roster evidence strong enough to prove the local player eliminated the relevant opposing participants.

A bomb lifecycle can produce explicit and generic events together:

```text
BOMB_PLANTED
...
BOMB_DEFUSED
BOMB_STATE_CLEARED
```

or:

```text
BOMB_PLANTED
...
BOMB_EXPLODED
BOMB_STATE_CLEARED
```

Multiple events from the same GSI payload remain one batch.

Modern session layout:

```text
sessions/session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw.gsi
```

Stop with `Ctrl+C`; Promator drains both the live-event queue and persistence queue before exit.

## Replay

```powershell
.\cspromator-probe.exe replay sessions\session_YYYYMMDD_HHMMSS 10
```

Append `dump` to print raw payloads. v0.0.1 session folders containing `raw/*.json` remain supported.

## Analyze an event stream offline

```powershell
.\cspromator-probe.exe analyze sessions\session_YYYYMMDD_HHMMSS
```

Offline analysis and the live pipeline use the same `normalize_gsi` + `EventDetector` implementation.

## Current trust boundary

CSPromator does not assume top-level `player` always means the local user. Local-player counters are accepted only when:

```text
player.steamid == provider.steamid
```

Joining a match in progress can expose spectated players before the local player's own state appears. Therefore observation of somebody else is not called `LOCAL_PLAYER_LOST` until local state has actually been acquired once.

The remembered local team survives spectating so `ROUND_WON` / `ROUND_LOST` cannot accidentally use the team of the player currently being observed.

### ACE policy in 0.0.5

```text
Competitive: 5 kills -> ACE_CANDIDATE (mode assumption)
Retakes CT:   3 kills -> ACE_CANDIDATE (mode assumption)
Retakes T:    4 kills -> ACE_CANDIDATE (mode assumption)
Casual:       no kill-count-only ace candidate
Other modes:  unknown until tested
```

A candidate is only produced for a kill belonging to the live round. Late post-round kills remain `PLAYER_KILL` but cannot manufacture an ace candidate.

Movement, live roster size, alive counts and spectator-only world state are not fabricated when active-player GSI does not provide them.

## Road ahead

```text
CS2 GSI
  -> raw recorder                    [working]
  -> state normaliser                [working first pass]
  -> transition / derived events     [working first pass]
  -> evidence-aware semantics        [working first pass]
  -> live event pipeline             [working]
  -> supplementary-state research    [next: Panorama capability discussion]
  -> director rule engine
  -> precise audio scheduler
  -> adaptive soundtrack engine
```

## Historical reference policy

The original CS-Jukebox project is historical evidence only. CSPromator is a clean implementation; its old CSGSI library, timers, playback engine, state machine and music-kit architecture are not copied.
