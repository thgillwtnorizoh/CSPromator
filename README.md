# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

The project is deliberately building the telemetry spine before the audio machinery. Prototype **0.0.4** records current CS2 Game State Integration traffic, interprets it live, and now carries the same generic event model across both normal competitive play and Retakes.

No audio engine is included yet. That is intentional.

## Prototype 0.0.4

The probe now:

- listens only on `127.0.0.1`;
- timestamps receive activity using `QueryPerformanceCounter` on Windows;
- acknowledges GSI before persistence, JSON parsing, event detection, or director work;
- fans each payload into independent persistence and live-interpretation worker queues;
- stores raw payload bytes in one packed `raw.gsi` stream;
- replays both 0.0.1 per-file sessions and modern packed sessions;
- normalizes local-player state only when `player.steamid == provider.steamid`;
- keeps local-player identity loss sticky across spectated-player and missing-player gaps;
- derives round/freeze transitions, win/loss, damage/death/respawn, kills/headshots, assists, flash/smoke/burning rising edges, ace, MVP, bomb plant/defuse, halftime, team swap and game over;
- keeps Retakes as mode context (`map.mode=retakes`) instead of forking the telemetry engine into Retakes-only events;
- prints derived event batches live during `record`;
- exposes `analyze` to run the exact same event detector over old sessions;
- includes anonymized regressions from three real test sessions.

See:

- [`docs/SESSION_001_FINDINGS.md`](docs/SESSION_001_FINDINGS.md)
- [`docs/SESSION_002_FINDINGS.md`](docs/SESSION_002_FINDINGS.md)
- [`docs/SESSION_003_FINDINGS.md`](docs/SESSION_003_FINDINGS.md)

## Get the Windows build without a local toolchain

GitHub Actions is the default build path. Every push to `main` builds Windows x64/MSVC, runs all regression tests plus the QPC smoke test, and uploads a portable artifact.

1. Open **Actions**.
2. Open the latest successful **Windows Build**.
3. Download `CSPromator-0.0.4-windows-x64`.
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
  PLAYER_KILL amount=1 value=2
  PLAYER_HEADSHOT_KILL amount=1 value=1
```

A Retakes round may instead produce a lifecycle such as:

```text
ROUND_STARTED
BOMB_PLANTED
PLAYER_SMOKED
PLAYER_BURNING
BOMB_DEFUSED
BOMB_STATE_CLEARED
ROUND_ENDED
ROUND_WON
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

The remembered local team survives spectating so `ROUND_WON` / `ROUND_LOST` cannot accidentally use the team of the bot currently being observed.

Retakes does **not** redefine three kills as an ace. Active-player GSI does not provide enough opponent-roster information to prove that assumption reliably.

Movement and spectator-only world state are not fabricated when active-player GSI does not provide them.

## Road ahead

```text
CS2 GSI
  -> raw recorder                    [working]
  -> state normaliser                [working first pass]
  -> transition / derived events     [working first pass]
  -> live event pipeline             [working]
  -> director rule engine
  -> precise audio scheduler
  -> adaptive soundtrack engine
```

## Historical reference policy

The original CS-Jukebox project is historical evidence only. CSPromator is a clean implementation; its old CSGSI library, timers, playback engine, state machine and music-kit architecture are not copied.
