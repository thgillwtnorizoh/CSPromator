# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

The project deliberately builds a trustworthy telemetry and semantic spine before the audio machinery. Prototype **0.0.6** keeps Game State Integration (GSI) as the required factual source and adds an optional supplementary-evidence layer for player-visible aggregate state that GSI cannot provide.

No live Panorama provider and no audio engine are included yet. Both omissions are intentional.

## Prototype 0.0.6

The probe now:

- listens for GSI only on `127.0.0.1`;
- timestamps receive activity using `QueryPerformanceCounter` on Windows;
- acknowledges GSI before persistence, JSON parsing, event detection, or director work;
- fans GSI payloads into independent persistence and live-interpretation worker paths;
- stores exact raw GSI payload bytes in packed `raw.gsi` sessions;
- accepts local-player numeric state only when `player.steamid == provider.steamid`;
- distinguishes initial `LOCAL_PLAYER_ACQUIRED` from later spectator `LOCAL_PLAYER_LOST` / `LOCAL_PLAYER_RESTORED` cycles;
- derives factual round, player, bomb, condition and outcome events from GSI;
- separates event evidence strength from event source provenance;
- keeps `ACE_CANDIDATE` as a GSI + mode-rule claim rather than asserting an unproven ace;
- defines a minimal optional supplementary contract containing only CT/T alive and total counts;
- exposes a **push** supplementary ingress in the live worker pipeline instead of a Panorama polling loop;
- uses a `roster_revision` to keep join/leave churn sticky even if team totals later return to the same value;
- can confirm evidence-backed `ACE` and derive `CLUTCH_STARTED`, `CLUTCH_UPDATED` and `CLUTCH_ENDED` when suitable supplementary observations are supplied;
- exposes semantic team-balance context (`local_alive`, `enemy_alive`, totals and alive delta) for the future Director;
- rejects invalid or out-of-order supplementary snapshots rather than rewinding state;
- includes regression coverage derived from four real GSI sessions plus synthetic supplementary-evidence scenarios.

See:

- [`docs/SESSION_001_FINDINGS.md`](docs/SESSION_001_FINDINGS.md)
- [`docs/SESSION_002_FINDINGS.md`](docs/SESSION_002_FINDINGS.md)
- [`docs/SESSION_003_FINDINGS.md`](docs/SESSION_003_FINDINGS.md)
- [`docs/SESSION_004_FINDINGS.md`](docs/SESSION_004_FINDINGS.md)
- [`docs/SUPPLEMENTARY_STATE_ARCHITECTURE.md`](docs/SUPPLEMENTARY_STATE_ARCHITECTURE.md)

## Get the Windows build without a local toolchain

GitHub Actions is the default build path. Every push to `main` builds Windows x64/MSVC, runs all regression tests plus the QPC smoke test, and uploads a portable artifact.

1. Open **Actions**.
2. Open the latest successful **Windows Build**.
3. Download `CSPromator-0.0.6-windows-x64`.
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

## Record + live GSI events

```powershell
.\cspromator-probe.exe record 3010 sessions
```

Example:

```text
[GSI] #42 t+37120.4 ms bytes=2190 ingress=31.8 ms ack=31.9 ms queued
[LIVE] #42 t+37120.4 ms
  PLAYER_KILL amount=1 value=5
  ACE_CANDIDATE value=5 evidence=mode-assumption sources=gsi+mode-rules
```

The public CLI does **not** connect to Panorama in 0.0.6. Therefore ordinary `record` behavior remains GSI-only and works without supplementary state.

Modern GSI session layout remains:

```text
sessions/session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw.gsi
```

Stop with `Ctrl+C`; Promator drains the live-event queue and persistence queue before exit.

## Factual events versus semantic evidence

GSI remains responsible for facts such as:

```text
PLAYER_KILL
PLAYER_DIED
ROUND_STARTED
ROUND_ENDED
ROUND_WON
ROUND_LOST
BOMB_PLANTED
BOMB_DEFUSED
BOMB_EXPLODED
```

A fixed mode rule may add:

```text
ACE_CANDIDATE
  evidence=mode-assumption
  sources=gsi+mode-rules
```

If a future legitimate supplementary provider supplies stable aggregate roster/alive evidence, the semantic resolver can later add:

```text
ACE
  sources=gsi+supplementary

CLUTCH_STARTED value=4
CLUTCH_UPDATED 4->2 value=2
CLUTCH_ENDED
```

This architecture keeps factual GSI events intact instead of creating a second competing event detector.

## Minimal supplementary contract

The first contract intentionally contains only:

```text
CT alive
T alive
CT total
T total
roster revision
```

It does not require names, Steam IDs, positions, inventories or hidden enemy data.

`roster_revision` increments when team membership changes. This means a sequence such as:

```text
5 enemies
-> 6 enemies
-> 5 enemies
```

still permanently marks that round as roster-changed for ACE confirmation.

Supplementary observations are pushed into the fusion worker. Promator core contains no Panorama polling loop.

## ACE policy

GSI-only candidate policy remains:

```text
Competitive: 5 kills -> ACE_CANDIDATE (mode assumption)
Retakes CT:   3 kills -> ACE_CANDIDATE (mode assumption)
Retakes T:    4 kills -> ACE_CANDIDATE (mode assumption)
Casual:       no kill-count-only ace candidate
Other modes:  unknown until tested
```

Confirmed `ACE` is a different semantic event. With supplementary evidence, the resolver requires a known round start, stable enemy roster revision/total, local round kills equal to the initial enemy total, and enemy alive count reaching zero. Casual can therefore be confirmed from actual stable roster evidence without inventing a fixed Casual threshold.

Late post-round kills remain factual `PLAYER_KILL` events and cannot manufacture a GSI ace candidate.

## Replay and analysis

GSI replay remains:

```powershell
.\cspromator-probe.exe replay sessions\session_YYYYMMDD_HHMMSS 10
```

Append `dump` to print raw payloads. v0.0.1 per-file sessions remain supported.

Offline GSI analysis remains:

```powershell
.\cspromator-probe.exe analyze sessions\session_YYYYMMDD_HHMMSS
```

0.0.6 does not yet persist supplementary observations because no runtime supplementary provider ships. Before any real provider is enabled, its observations must gain deterministic recording/replay support. See the supplementary architecture document.

## Trust boundary

CSPromator does not assume top-level `player` always means the local user. Local-player counters are accepted only when:

```text
player.steamid == provider.steamid
```

The remembered local team survives spectating so outcome semantics cannot use the team of the currently spectated player.

Supplementary state is allowed to improve knowledge, never to become a hidden-information oracle. A future provider may only supply ordinary player-visible aggregate information through a supported, external, non-invasive mechanism.

Do not use DLL injection, hooks, game-memory reads, `panorama.dll` patching, VPK/HUD transmitter modifications or hidden enemy world state.

If a supplementary provider is absent, broken or outdated, Promator must degrade to normal GSI-only behavior rather than failing.

## Road ahead

```text
CS2 GSI
  -> raw recorder                    [working]
  -> state normaliser                [working first pass]
  -> transition / factual events     [working first pass]
  -> evidence-aware semantics        [working first pass]
  -> live event pipeline             [working]
  -> supplementary fusion contract   [working, provider unplugged]
  -> supported Panorama bridge?      [research only]
  -> director rule engine
  -> precise audio scheduler
  -> adaptive soundtrack engine
```

## Historical reference policy

The original CS-Jukebox project is historical evidence only. CSPromator is a clean implementation; its old CSGSI library, timers, playback engine, state machine and music-kit architecture are not copied.
