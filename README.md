# Granadad: The Darkstreets

*(project codename: Flame of Mercolas)*

A simulation-first game set in **Granadad**, capital of Trojia — from the novel *Lord of Trojia*.
You will play **Gabri, Wielder of the Flame of Mercolas**: socially untouchable from day one,
physically weak until you learn to bend the world's systems.

## v0 scope contract — simulation before gameplay

v0 is a Dwarf Fortress-depth **physical simulation** plus a god-view observer client.

**In v0:** materials & properties, temperature & fire, fluids, light, ledger economy,
true z-levels, active-bubble fidelity, deterministic turn-based ticks.

**Explicitly NOT in v0:** no player character, no agents/NPCs, no items-as-entities,
no combat, no quests. Any PR/commit adding gameplay to v0 is out of scope by definition.

## Design north star

> Social power is maxed from the start; physical power starts near zero and grows through
> exploitable systems — never through levels. Authority/law is a first-class domain concept.

## Modules

| Module | Package | Purpose |
|---|---|---|
| `sim-core` | `com.trojia.sim.*` | Pure-Java simulation engine. **Zero libGDX/AWT** (ArchUnit-enforced). |
| `content` | resources only | Material raws (JSON), Tiled sources, baked maps, placeholder art. |
| `tools` | `com.trojia.tools.*` | Tiled importer (`.tmj` → `.tregion`), raws validator. |
| `headless` | `com.trojia.headless.*` | CLI scenario runner + benchmarks. Depends on sim-core only. |
| `client-observer` | `com.trojia.client.*` | libGDX god-view observer (LWJGL3). |

## Running

```
gradlew build                    # compile + all tests (unit, scenario, ArchUnit)
gradlew :headless:run            # headless simulation heartbeat
gradlew :client-observer:run     # the observer window
```

## Canon

Lore lives in the sibling repo `..\LordOfTrojia-MVP` (read-only reference; the novel always wins;
the `Lore\*.html` files there are non-canon). Derived game data in this repo carries provenance notes.
