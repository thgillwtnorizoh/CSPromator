# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

The project is deliberately building the telemetry spine before the audio machinery. Current prototype **0.0.2** records current CS2 Game State Integration traffic, replays old/new sessions, normalizes the safe active-player subset, and derives a first deterministic event stream.

No audio engine is included yet. That is intentional.

## Prototype 0.0.2

The probe now:

- listens only on `127.0.0.1`;
- timestamps receive activity using `QueryPerformanceCounter` on Windows;
- acknowledges GSI before persistence or interpretation;
- queues persistence on a separate worker so disk stalls cannot delay the next `accept()` timestamp;
- stores raw payload bytes in one packed `raw.gsi` stream to reduce tiny-file disk overhead;
- replays both 0.0.1 per-file sessions and 0.0.2 packed sessions;
- normalizes active-player state only when `player.steamid == provider.steamid`;
- derives first-pass events such as round/freeze transitions, damage/death, kills/headshots, ace, MVP, bomb plant, halftime, team swap and game over;
- exposes `analyze` to print event batches from a recorded session;
- includes anonymized regression tests based on the first real four-round capture.

See [`docs/SESSION_001_FINDINGS.md`](docs/SESSION_001_FINDINGS.md) for what the first real session taught us.

## Get the Windows build without a local toolchain

GitHub Actions is the default build path. Every push to `main` builds Windows x64/MSVC, runs the regression suite plus the QPC smoke test, and uploads a portable artifact.

1. Open **Actions**.
2. Open the latest successful **Windows Build**.
3. Download `CSPromator-0.0.2-windows-x64`.
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

## Record

```powershell
.\cspromator-probe.exe record 3010 sessions
```

0.0.2 session layout:

```text
sessions/session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw.gsi
```

Stop with `Ctrl+C`; Promator drains the persistence queue before exit.

## Replay

```powershell
.\cspromator-probe.exe replay sessions\session_YYYYMMDD_HHMMSS 10
```

Append `dump` to print raw payloads. v0.0.1 session folders containing `raw/*.json` remain supported.

## Analyze an event stream

```powershell
.\cspromator-probe.exe analyze sessions\session_YYYYMMDD_HHMMSS
```

Events are emitted as **batches per GSI snapshot**, because real CS2 can report kill, ace, MVP, round-end and halftime changes in the same payload.

## Current trust boundary

CSPromator does not assume top-level `player` always means the local user. After death, CS2 can make that object follow a spectated player. Local-player counters are therefore accepted only when:

```text
player.steamid == provider.steamid
```

Likewise, movement and spectator-only world state are not fabricated when active-player GSI does not provide them.

## Road ahead

```text
CS2 GSI
  -> raw recorder                    [working]
  -> state normaliser                [first pass]
  -> transition / derived events     [first pass]
  -> live event pipeline
  -> director rule engine
  -> precise audio scheduler
  -> adaptive soundtrack engine
```

## Historical reference policy

The original CS-Jukebox project is historical evidence only. CSPromator is a clean implementation; its old CSGSI library, timers, playback engine, state machine and music-kit architecture are not copied.
