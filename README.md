# CSPromator 🍅

**Promator** is an experimental adaptive-audio director for Counter-Strike 2.

This repository starts deliberately smaller than the eventual idea. **Milestone 0 is a GSI telemetry recorder/profiler** whose job is to establish what current CS2 sends to a normal external companion application and when it arrives.

No audio engine is included yet. That is intentional.

## Prototype 0.0.1: GSI Probe

The probe:

- listens only on `127.0.0.1`;
- accepts CS2 Game State Integration HTTP POSTs;
- timestamps receive activity using `QueryPerformanceCounter` on Windows;
- stores every raw JSON snapshot unchanged;
- records an append-only timeline with accept/body/ACK/persist timing;
- acknowledges GSI **before disk I/O** and without gameplay interpretation or audio work;
- replays the captured snapshot timing later without CS2 running.

## Get the Windows build without a local toolchain

GitHub Actions is the default build path for the prototype. Every push to `main` builds the Windows x64/MSVC version, runs a QPC clock smoke test, and uploads a portable ZIP artifact.

1. Open the repository's **Actions** tab.
2. Open the latest successful **Windows Build** run.
3. Download the `CSPromator-0.0.1-windows-x64` artifact.
4. Extract `CSPromator-0.0.1-windows-x64.zip` from the downloaded artifact.

The portable package contains the probe executable, GSI configuration, installer script, README, and documentation. No Visual Studio or CMake installation is required on the target machine.

The workflow can also be started manually with **Actions → Windows Build → Run workflow**.

### Optional local build on Windows

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Binary:

```text
build\Release\cspromator-probe.exe
```

### Install the GSI probe configuration

Either copy:

```text
config\gamestate_integration_cspromator.cfg
```

to:

```text
<Counter-Strike Global Offensive>\game\csgo\cfg\
```

or run:

```powershell
.\scripts\install-gsi.ps1 -Cs2GameRoot "C:\...\steamapps\common\Counter-Strike Global Offensive\game"
```

Restart CS2 if it was already open.

### Record

From the portable GitHub Actions package:

```powershell
.\cspromator-probe.exe record 3010 sessions
```

From a local CMake build:

```powershell
.\build\Release\cspromator-probe.exe record 3010 sessions
```

A session looks like:

```text
sessions/session_YYYYMMDD_HHMMSS/
  metadata.json
  timeline.tsv
  raw/
    00000001.json
    00000002.json
    ...
```

### Replay

Portable package:

```powershell
.\cspromator-probe.exe replay sessions\session_YYYYMMDD_HHMMSS 1.0
```

Fast replay:

```powershell
.\cspromator-probe.exe replay sessions\session_YYYYMMDD_HHMMSS 10
```

Append `dump` to print each raw payload during replay.

## Why the first version is this boring

CSPromator's eventual path is roughly:

```text
CS2 GSI
  -> raw recorder
  -> state normaliser
  -> transition / derived-event engine
  -> director rule engine
  -> precise audio scheduler
  -> adaptive soundtrack engine
```

The first four arrows are useless if the first one is poorly understood. We therefore measure modern CS2 behaviour before building musical logic around decade-old assumptions.

## Historical reference policy

The original CS-Jukebox project is useful historical evidence, but CSPromator is a clean implementation. We may learn from its behaviour and pain points; we do not copy its CSGSI library, timers, playback engine, state machine, or music-kit architecture.
