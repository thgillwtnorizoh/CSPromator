# CSPromator edition architecture

CSPromator 0.0.8 deliberately splits the product into two editions while keeping one shared core.

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

The editions are separate build artifacts, not long-lived source branches. Event detection, lifecycle, session recording/replay, semantic resolution and future Director/audio code remain shared.

## CSPromator GSI

The GSI edition follows one rule:

> If GSI does not prove it, this edition does not know it.

It intentionally contains only the required Game State Integration provider. This preserves the small failure surface that has already worked across the real-session regression set.

Available behavior includes factual GSI-derived events, lifecycle context, deterministic recording/replay and mode-rule `ACE_CANDIDATE` semantics. Information that requires another source remains unknown rather than guessed.

The GSI edition must never silently acquire dependencies on Steam Game Recording, Panorama-visible state, Overwolf or another supplementary provider.

## CSPromator Extended

Extended keeps GSI as its required factual spine and provides a home for optional evidence sources.

Current 0.0.8 provider catalog:

| Provider | Status | Purpose |
| --- | --- | --- |
| Game State Integration | active, required | factual spine |
| Visible Team State | planned | CT/T alive and total counts |
| Steam Timeline | planned | semantic event enrichment |
| CS2 Round End Report | research-only | post-round analytical truth |
| CS2 Current Round Odds | research-only | protocol research; quarantined from Director use |
| CS2 Deep Stats | research-only | historical analytics |

Only `active` providers contribute live capability. A `planned` or `research-only` provider being listed does not mean it is implemented, connected or trusted.

Therefore CSPromator Extended 0.0.8 currently behaves live like the GSI edition and explicitly reports that fallback in `status` and `record` output.

## Capability model

Provider maturity and capability are separate concepts.

Examples:

```text
GSI
  status: active
  capabilities:
    gsi-facts
    deterministic-replay

Steam Timeline
  status: planned
  capability target:
    steam-timeline-events
```

`active_capabilities()` includes only active providers. `potential_capabilities()` describes the research/design target of an edition and must never be used as proof that data is currently available.

This prevents a future rule or Director component from treating an experimental provider declaration as live evidence.

## Failure rule

Extended must always fail open to GSI:

```text
optional provider healthy
        |
        v
GSI + optional evidence

optional provider unavailable / stale / unsupported
        |
        v
GSI factual behavior continues unchanged
```

No optional provider may be required to start recording, replay an ordinary GSI session, detect factual GSI transitions or stop/flush a session cleanly.

## Session identity

0.0.8 keeps session schema v3 unchanged. New `metadata.json` files add the producing edition:

```json
{
  "schema_version": 3,
  "application": "CSPromator",
  "application_version": "0.0.8",
  "edition": "gsi"
}
```

or:

```json
"edition": "extended"
```

This is metadata only and does not change `timeline.tsv`, `raw.gsi` or `supplementary.timeline.tsv`.

A schema-v4 external-observation stream is intentionally deferred until at least one real provider has been measured. In particular, Steam Game Recording timeline latency and write behavior must be established before its storage contract is frozen.

## Trust boundary

The edition split does not weaken the existing trust boundary.

Do not use DLL injection, process hooks, game-memory reads, `panorama.dll` patching, stock HUD/VPK transmitter modifications, anti-cheat bypasses or hidden-information sources.

A future Extended provider must be external, non-invasive, recordable/replayable before behavioral use, and limited to information appropriate for the local player.

`CurrentRoundOdds` remains research-only because its model inputs are not yet understood. Even if an external bridge is found, it must not drive adaptive audio until it is established that doing so would not create a hidden-information oracle.

## Release rule

Both editions share one release number.

For 0.0.8 the Windows artifacts are:

```text
CSPromator-0.0.8-GSI-windows-x64
CSPromator-0.0.8-Extended-windows-x64
```

A shared-core fix therefore ships to both editions together, while an experimental provider can remain isolated to Extended.
