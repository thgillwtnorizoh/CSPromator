# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

The project builds a trustworthy telemetry and semantic spine before the audio machinery. Version **0.0.8** splits the product into two editions without splitting the codebase:

```text
                         cspromator-core
                               |
                  +------------+------------+
                  |                         |
                  v                         v
          CSPromator GSI             CSPromator Extended
          cspromator-gsi.exe         cspromator-extended.exe
                  |                         |
                  v                         v
             GSI only                GSI + optional providers
```

The same EventDetector, SemanticResolver, lifecycle model, session recorder/replay and future Director/audio engine are shared by both editions.

See [`docs/EDITION_ARCHITECTURE.md`](docs/EDITION_ARCHITECTURE.md) for the edition contract.

## 0.0.8 editions

### CSPromator GSI

The dependable edition.

Rule:

> If GSI does not prove it, this edition does not know it.

It contains only the required CS2 Game State Integration provider and preserves the small, deterministic telemetry path established in previous prototypes.

It can derive factual local/round/bomb/lifecycle events, record/replay sessions, and emit mode-rule semantics such as `ACE_CANDIDATE`. It does not pretend to know live team counts, victim identity or other information GSI withholds.

### CSPromator Extended

The experimental edition.

It keeps GSI as the required factual spine and provides a home for optional evidence providers. In 0.0.8 the provider catalog includes:

```text
Game State Integration      active / required
Visible Team State          planned
Steam Timeline              planned
CS2 Round End Report        research-only
CS2 Current Round Odds      research-only
CS2 Deep Stats              research-only
```

Only **active** providers contribute live capability. Planned/research providers are listed so future work has an explicit home; they are not silently treated as connected or trusted.

Therefore Extended 0.0.8 currently degrades to the same live GSI behavior as the GSI edition.

## Status command

Both binaries expose their edition and provider maturity:

```powershell
.\cspromator-gsi.exe status
.\cspromator-extended.exe status
```

The GSI build reports only the active GSI provider.

The Extended build reports GSI plus planned/research providers, and separates **active capabilities** from **Extended capability targets** so an unfinished provider cannot accidentally become evidence.

## Current telemetry spine

GSI remains authoritative for facts such as:

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

The core also:

- listens only on `127.0.0.1`;
- timestamps ingress using `QueryPerformanceCounter` on Windows;
- acknowledges GSI before persistence, JSON parsing, event detection or future Director work;
- stores exact raw GSI bytes in packed `raw.gsi` sessions;
- accepts local numeric state only when `player.steamid == provider.steamid`;
- distinguishes initial `LOCAL_PLAYER_ACQUIRED` from spectator loss/restore cycles;
- preserves warmup/post-round factual events instead of renaming them;
- exposes lifecycle context: detached, warmup, freezetime, live-round, post-round, gameover and other;
- keeps evidence strength separate from source provenance;
- keeps `ACE_CANDIDATE` distinct from evidence-backed `ACE`;
- supports optional pushed supplementary team-count evidence;
- records/replays GSI + supplementary observations in canonical cross-source ingress order.

See [`docs/SUPPLEMENTARY_STATE_ARCHITECTURE.md`](docs/SUPPLEMENTARY_STATE_ARCHITECTURE.md).

## Windows builds

GitHub Actions builds Windows x64/MSVC, runs the full regression suite and smoke-tests both editions.

Artifacts for 0.0.8:

```text
CSPromator-0.0.8-GSI-windows-x64
CSPromator-0.0.8-Extended-windows-x64
```

Each portable package contains its edition executable, GSI configuration, installer script, README and docs.

### Optional local build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Executables:

```text
build\Release\cspromator-gsi.exe
build\Release\cspromator-extended.exe
```

## Install GSI

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

Both editions use the same GSI configuration because Extended still requires the same factual spine.

## Record live GSI

GSI edition:

```powershell
.\cspromator-gsi.exe record 3010 sessions
```

Extended edition:

```powershell
.\cspromator-extended.exe record 3010 sessions
```

In 0.0.8 Extended prints that experimental providers are unplugged and then runs the same GSI path.

Example event output:

```text
[LIVE] #42 t+37120.4 ms
  PLAYER_KILL amount=1 value=5
  ACE_CANDIDATE value=5 evidence=mode-assumption sources=gsi+mode-rules
```

Stop with `Ctrl+C`; Promator drains queued live/session work before exit.

## Session format

0.0.8 intentionally keeps **schema v3**. The storage shape remains:

```text
sessions/session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw.gsi
  supplementary.timeline.tsv
```

New metadata records the producing edition:

```json
{
  "schema_version": 3,
  "application": "CSPromator",
  "application_version": "0.0.8",
  "edition": "gsi"
}
```

or `"edition": "extended"`.

No schema-v4 external-observation stream is frozen yet. Steam Timeline write behavior and other live provider bridges must be measured before their persistent contract is committed.

## Replay and analysis

Either edition can replay existing v1/v2/v3 sessions:

```powershell
.\cspromator-gsi.exe replay sessions\session_YYYYMMDD_HHMMSS 10
.\cspromator-extended.exe replay sessions\session_YYYYMMDD_HHMMSS 10
```

Output distinguishes:

```text
[REPLAY GSI]
[REPLAY SUPPLEMENT]
```

Append `dump` to print raw GSI payloads.

Offline semantic analysis:

```powershell
.\cspromator-gsi.exe analyze sessions\session_YYYYMMDD_HHMMSS
```

The same factual + semantic stack is used during live processing and replay.

## Lifecycle instead of event-name explosion

Promator keeps facts factual and exposes lifecycle beside them:

```text
PLAYER_KILL + lifecycle=warmup
PLAYER_KILL + lifecycle=live-round
PLAYER_KILL + lifecycle=post-round
```

Future Director rules can use:

```text
scored_round_active == true
```

instead of requiring separate event names for every phase.

Match leave/reset clears match-local supplementary state before another map can attach.

## Minimal supplementary contract

The existing optional supplementary contract remains deliberately small:

```text
CT alive
T alive
CT total
T total
roster revision
```

It contains no names, Steam IDs, positions or inventories.

A provider increments `roster_revision` when team membership changes. Alive-count changes alone do not increment it. This prevents a `5 -> 6 -> 5` roster change from becoming invisible simply because the final count matches the initial count.

Suitable supplementary evidence may produce:

```text
ACE
CLUTCH_STARTED
CLUTCH_UPDATED
CLUTCH_ENDED
```

while GSI-only behavior remains unchanged when that provider is absent.

## ACE policy

GSI-only candidate policy currently remains:

```text
Competitive: 5 kills -> ACE_CANDIDATE (mode assumption)
Retakes CT:   3 kills -> ACE_CANDIDATE (mode assumption)
Retakes T:    4 kills -> ACE_CANDIDATE (mode assumption)
Casual:       no kill-count-only ace candidate
Other modes:  unknown until tested
```

Confirmed `ACE` remains a separate semantic claim requiring suitable supplementary evidence.

## Trust boundary

Promator's edition split does not weaken the trust boundary.

Do not use:

- DLL injection;
- process hooks;
- game-memory reads;
- `panorama.dll` patching;
- stock HUD/VPK transmitter modifications;
- anti-cheat bypasses;
- hidden enemy world state.

Extended providers must be external, non-invasive and recordable/replayable before they affect behavior.

`CurrentRoundOdds` is explicitly research-only because its underlying model inputs are not yet understood. Finding a bridge would not automatically make it acceptable Director input.

## Regression evidence

The repository contains anonymized regression coverage derived from five real GSI sessions plus synthetic supplementary scenarios. These cover competitive, Retakes, Casual, spectator transitions, multi-match lifecycle behavior, bomb transitions, stale supplementary evidence and deterministic cross-source replay.

See:

- [`docs/SESSION_001_FINDINGS.md`](docs/SESSION_001_FINDINGS.md)
- [`docs/SESSION_002_FINDINGS.md`](docs/SESSION_002_FINDINGS.md)
- [`docs/SESSION_003_FINDINGS.md`](docs/SESSION_003_FINDINGS.md)
- [`docs/SESSION_004_FINDINGS.md`](docs/SESSION_004_FINDINGS.md)
- [`docs/SESSION_005_FINDINGS.md`](docs/SESSION_005_FINDINGS.md)

## Road ahead

```text
shared core                         [working]
GSI edition                         [0.0.8]
Extended edition shell              [0.0.8]
GSI record/replay                   [working]
supplementary fusion contract       [working, live provider unplugged]
provider capability/maturity model  [0.0.8]
Steam Timeline live-write test      [next research]
Steam Timeline provider             [only if live behavior is suitable]
visible-team-state bridge           [research]
round-report external bridge        [research]
Director rule engine
precise audio scheduler
adaptive soundtrack engine
```

## Historical reference policy

The original CS-Jukebox project is historical evidence only. CSPromator is a clean implementation; its old CSGSI library, timers, playback engine, state machine and music-kit architecture are not copied.
