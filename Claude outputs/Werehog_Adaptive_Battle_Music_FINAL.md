# Werehog Adaptive Battle Music — Final Documentation

**Status: COMPLETE.** The synchronized-timeline battle music system described
below is implemented and CONFIRMED working end-to-end by direct in-game
testing (normal-first level starts, immediate/early-battle encounters,
repeated battles, death/respawn, and stage restart). This document is the
single reference for how it works and how to extend it. It supersedes the
architecture sections of `00_README_FIRST.md` — that file remains useful as
a dated research log (what was tried, what failed, why), but for "how does
the shipped mod actually work," use this document.

Going forward, the only expected changes to this system are content
additions (new stage-specific remixes) and, eventually, whatever changes
become possible if/when this game gets an official code-modding API. The
synchronization core itself should not need further changes.

---

## 1. What this mod does

Vanilla Werehog battle music starts from 0:00 the instant a fight begins,
and stops (or gets abruptly reset) when it ends. This mod makes Werehog
battle music behave like Sonic Unleashed's **daytime boost music** instead:
one continuous soundtrack with a hidden alternate layer that the game
reveals and hides on cue, never restarting.

Concretely:

- The normal stage theme (`evil_normal`) and a battle remix both start
  playing at the same point in the stage's lifecycle and advance on a single,
  shared timeline for the rest of the stage.
- The battle remix is silent by default. The game's own native volume/fade
  system (unmodified) is what reveals it when a fight starts and hides it
  again when the fight ends.
- The battle remix is **never restarted**. Whether it's your first fight of
  the stage or your fifteenth, it's still the same underlying player object,
  still on the same timeline it's been on since it started.

The one accepted limitation: if a fight breaks out **immediately** at level
spawn, before the normal theme has ever actually started, that one specific
encounter plays without battle music (silence, native fallback covered
below). Every fight after that plays correctly, in sync, for the rest of the
stage. Section 6.2 explains why this is a deliberate tradeoff, not a bug.

---

## 2. The core mental model

If you remember nothing else from this document, remember this:

```text
D970  = arm/select a cue on a player (does NOT start playback)
D528  = activate/start a player (via its vtable+8 slot)
D778  = reset/stop/reinitialize a player

soundData + 8  = normalPlayer   (the stage's normal theme player)
soundData + 16 = battlePlayer   (the regular battle-music player)
soundData + 24/32/40 = special players, role UNKNOWN (see Section 8)
```

```text
evil_normal armed on normalPlayer
        |
        +--> battle cue armed on battlePlayer, same lifecycle moment

normalPlayer really activates (D528(normalPlayer, 1))
        |
        +--> battlePlayer activates immediately afterward

both timelines now run together, forever, for this stage
        |
        +--> outside battle: battlePlayer silent (native mixer)
        +--> in battle:       battlePlayer audible (native mixer)
        +--> native battle-entry reset/re-arm/re-start attempts on
             battlePlayer are all suppressed once persistence has begun
```

Nothing here polls, times, or watches a "currently in battle" flag. The only
synchronization event is `normalPlayer`'s own real activation call — that
call is the trigger that starts `battlePlayer` alongside it.

---

## 3. Confirmed data layout

```text
owner + 156  ->  soundData        [CONFIRMED]

soundData + 8   = normalPlayer    [CONFIRMED]
soundData + 16  = battlePlayer    [CONFIRMED]
soundData + 24  = special player  [UNKNOWN role — shares battlePlayer's vtable, see Section 8]
soundData + 32  = special player  [UNKNOWN role — armed with a cue during setup itself, see Section 8]
soundData + 40  = special player  [UNKNOWN role]

soundData + 168 = battle-state byte, meaning UNKNOWN (see Section 8)
soundData + 169 = battle-state byte, meaning UNKNOWN (see Section 8)
```

`owner` is the Werehog gameplay/context object. `soundData` is a distinct
object reached one guest-memory hop away from it. All of the above are
**guest addresses** — see Section 5 before touching any of them directly in
new code.

---

## 4. Function reference

| Function | Role | Notes |
|---|---|---|
| `sub_82B48548` | Werehog audio setup — creates/initializes the player slots (`+8`…`+40`) on `soundData`. | Call the original **first**, then read the slots — they don't exist until the original runs. This is where the mod captures `g_werehogNormalPlayer` / `g_werehogBattlePlayer` and resets all lifecycle flags for the new stage. |
| `sub_82B4D970` (D970) | Arms/selects a cue on a player. `r3 = player`, `r4 = guest address of cue-name string`. Does not start playback. | **Global** — fires for every cue-selection in the whole game, not just Werehog. Must guard `r4 == 0`. |
| `sub_82B4D528` (D528) | Activates/starts a player (`r3 = player`, `r4 = value`, `value == 1` = start). Reached through the player's `vtable+8` slot. | **Global** — fires for every player activation in the whole game. Must guard `player == 0`. This is the mod's synchronization point. |
| `sub_82B4D778` (D778) | Resets/stops/reinitializes a player. | Native battle entry calls this on `battlePlayer`; suppressing it (selectively) is what keeps the persistent timeline alive across repeated battles. |
| `sub_82B465C8` | Native battle-entry transition function. | Wrapped only to scope `g_insideWerehogBattleEntry` around the native call — used by the D970/D778 filters above to tell battle-entry calls apart from ordinary gameplay calls. |
| `sub_82B478E0` | Native transition that (among other things) re-activates `battlePlayer`. | No dedicated wrapper needed — its activation call dispatches through the same `vtable+8` → `sub_82B4D528` path, so the D528 suppression check catches it automatically. |
| `WerehogBattleCueTestMidAsmHook` | MIDASM hook at `0x82B46614` that overwrites the cue name register with the mod's own cue during native battle-entry cue selection. | Under the current architecture `battlePlayer` can never actually be started through this path (D528 blocks it unconditionally, see Section 6), so this hook is functionally inert. Kept in place; harmless either way. |
| `GetStageBattleCueName()` / `LoadStageBattleCues()` | Resolve the current stage ID to a custom battle cue name, loaded at runtime from `stage_battle_cues.json`. Falls back to native `evil_battle1` for any unmapped stage. | This is the extension point for adding new remixes — see Section 7. |

---

## 5. Guest vs. host memory (quick primer for new contributors)

This game runs through a recompiler: original Xbox 360 code becomes native
PPC-shaped C++ functions, but the *data* those functions manipulate still
lives in a separate, emulated "guest" address space, not directly as normal
C++ objects.

```text
guest/Xbox address  --Translate-->  host/PC pointer   (read it from C++)
host/PC pointer      --MapVirtual--> guest/Xbox address  (hand it to PPC code)
```

Never dereference a guest address like a normal pointer
(`*(SomeStruct*)owner`). To read a 32-bit field out of a guest object, use:

```cpp
uint32_t soundData = PPC_LOAD_U32(owner + 156);
```

Guest address `0` is special: this engine reserves guest memory starting at
host address `0x100000000`, so guest `0` maps to host `0x100000000` exactly
— a deliberately unmapped guard page. A crash at precisely that address
means something dereferenced a guest-null pointer without checking it
first. This is why the D970/D528 wrappers below both guard their inputs
before touching them — they're global hooks, so an unrelated system in the
game can and will call them with `player == 0` or `r4 == 0` for reasons
that have nothing to do with Werehog music.

---

## 6. The synchronization architecture

Four flags drive the whole system:

```cpp
static uint32_t g_werehogNormalPlayer = 0;
static uint32_t g_werehogBattlePlayer = 0;
static uint32_t g_werehogSpecialPlayer = 0;

static bool g_werehogBattlePrimed = false;              // battlePlayer has a cue armed
static bool g_werehogPersistentBattleStarted = false;    // battlePlayer is running persistently
static bool g_insideWerehogBattleEntry = false;          // currently inside native battle-entry transition
static bool g_werehogEarlyPairPrepared = false;          // cue armed for an early-battle start, not yet activated
static bool g_werehogWaitingForNativeNormalCatchup = false; // both players force-started early; swallow native's later, redundant normalPlayer calls once
```

All four state flags reset to `false` once per stage load, inside
`sub_82B48548`, alongside re-capturing the player identities. That's the
only place they ever reset outside of the stage-restart detection in
Section 6.3.

### 6.1 Scenario A — normal case (level starts, no battle for a while)

```text
stage loads
        |
sub_82B48548 captures normalPlayer / battlePlayer, resets all flags
        |
native arms "evil_normal" on normalPlayer  ->  D970 wrapper
        |
        +--> battlePlayer doesn't have a cue yet (!g_werehogBattlePrimed)
        +--> arm the stage's battle cue on battlePlayer too, right here
        +--> g_werehogBattlePrimed = true

... time passes, both players are armed but neither has started yet ...

native actually activates normalPlayer: D528(normalPlayer, 1)  ->  D528 wrapper
        |
        +--> let native's own activation happen (never suppressed)
        +--> battlePlayer hasn't started yet (!g_werehogPersistentBattleStarted)
        +--> activate battlePlayer immediately afterward, same call
        +--> g_werehogPersistentBattleStarted = true

both players now advance on the same timeline for the rest of the stage.
battle begins/ends from here on out: native fade system reveals/hides
battlePlayer. Nothing in the mod touches volume.
```

This is the path that runs for the overwhelming majority of play — any
stage where the player doesn't get ambushed in the first few seconds.

### 6.2 Scenario B — a fight breaks out before normalPlayer ever activates

This is the case that required real design work, because two earlier
approaches were tried and both failed for structural, not implementation,
reasons:

- **Letting `battlePlayer` cold-start on its own** (native's own D970/D528,
  untouched) fixed nothing — the wrong (native, non-custom) cue would play,
  and once it was running, later attempts to bring it into sync would call
  D528 on an already-active player, which does **not** restart or reselect
  its cue — so it stayed frozen on the wrong cue for the rest of the stage.
- **Force-starting `battlePlayer` early with the correct custom cue** fixed
  the cue problem, but broke timeline sync: `battlePlayer`'s clock and
  `normalPlayer`'s clock would now start at two different real times, and
  there's no reliable way to re-sync two already-running audio players
  after the fact without producing an audible restart/layering artifact —
  confirmed by direct testing.

**There is no way to have both "no restart" and "perfectly synced
timelines" once `battlePlayer` has already started running before
`normalPlayer` has.** The adopted design instead prevents `battlePlayer`
from ever starting through any path except the one already proven correct
in Scenario A:

```text
battle entry begins before normalPlayer has ever been primed
        |
native arms a cue on battlePlayer (battle-entry cue selection)  ->  D970 wrapper
        |
        +--> g_insideWerehogBattleEntry && !g_werehogBattlePrimed
        +--> EARLY PAIR: arm "evil_normal" on normalPlayer directly (synthetic call)
        +--> arm the stage's battle cue on battlePlayer directly (synthetic call)
        +--> g_werehogBattlePrimed = true
        +--> g_werehogEarlyPairPrepared = true
        +--> return WITHOUT letting native's own attempted cue-arm through

native then tries to activate battlePlayer (battle-entry activation)  ->  D528 wrapper
        |
        +--> g_werehogEarlyPairPrepared && !g_werehogPersistentBattleStarted
        +--> EARLY PAIR START: activate normalPlayer AND battlePlayer together,
             right here, synthetic calls to both
        +--> g_werehogPersistentBattleStarted = true
        +--> g_werehogEarlyPairPrepared = false
        +--> g_werehogWaitingForNativeNormalCatchup = true

later, native's own (now redundant) D970("evil_normal", normalPlayer) and
D528(normalPlayer, 1) calls eventually arrive on their own
        |
        +--> g_werehogWaitingForNativeNormalCatchup is true
        +--> both calls are swallowed exactly once, so normalPlayer isn't
             re-armed/restarted out from under the pair we already started
        +--> g_werehogWaitingForNativeNormalCatchup = false (D528 wrapper resets it)
```

Both players end up started together, at the same instant, on the same
timeline — indistinguishable from Scenario A from that point forward.

### 6.3 Scenario C — stage restart / checkpoint reload

If `battlePlayer` receives a native D778 (reset) call **outside** battle
entry while the mod still believes persistence is active, that's not a
normal battle-entry reset (those are already suppressed elsewhere) — it's a
sign the underlying player object's playback state was invalidated by
something external, e.g. a stage restart reusing the same player objects.

```text
D778 arrives for battlePlayer, g_insideWerehogBattleEntry == false,
g_werehogPersistentBattleStarted == true
        |
        +--> let the native reset happen (do not suppress it — it's real here)
        +--> treat this as a hard invalidation: reset all four state flags
             (g_werehogBattlePrimed, g_werehogPersistentBattleStarted,
              g_werehogEarlyPairPrepared, g_werehogWaitingForNativeNormalCatchup)
        |
next stage-start lifecycle rebuilds the pair cleanly via Scenario A or B
```

### 6.4 Suppression summary — what's blocked, and only when

```text
D970 on battlePlayer, inside battle entry, persistence already started:
    blocked — the persistent cue must survive re-encounters

D528 on battlePlayer, persistence already started:
    blocked — battlePlayer must never restart once it's running

D778 on battlePlayer, inside battle entry, persistence already started:
    blocked — native's own reset-on-battle-entry would kill the timeline

D778 on battlePlayer, OUTSIDE battle entry, persistence already started:
    allowed through, then treated as a stage-restart signal (Section 6.3)
```

Nothing is ever suppressed unconditionally — every check is scoped to the
actual saved player identity and the specific lifecycle moment. A global
"ignore all D528 calls" approach was tried earlier in development and
produced unpredictable, hard-to-debug side effects on unrelated systems;
don't reintroduce that pattern.

---

## 7. How to add a new stage-specific remix (for modders, no C++ required)

The cue-to-stage mapping is data-driven and requires no recompiling.

1. Author your remix as a cue inside the `bgm_stg_e_btl` bank (the one bank
   the battle player actually searches — CONFIRMED it does not fall back to
   any other simultaneously-loaded bank, so adding the cue to a stage's own
   bank instead of this one will not work).
2. **Make sure the cue's audio reference is actually set** in the authoring
   tool, not just the cue entry created. An empty/unlinked cue name
   resolves fine but crashes the game outright the moment it's triggered —
   this engine's reimplemented audio layer does not null-check a cue with
   no attached material. "The entry exists" is not sufficient; confirm it
   actually plays when triggered manually first.
3. Add an entry to `stage_battle_cues.json` (same folder as the game
   executable): a flat JSON object mapping the stage's internal ID to your
   cue's name, e.g.:
   ```json
   { "ActN_MykonosEvil": "myk_e_btl", "ActN_SnowEvil": "snw_e_btl" }
   ```
   Standard JSON only — no comments, no trailing commas, colons not commas
   between key and value.
4. That's it. No recompile needed. Any stage without an entry automatically
   falls back to the native `evil_battle1` cue, so a partially-mapped mod
   never produces silence for an unmapped stage.

If the file is missing entirely, or a single entry is malformed, the loader
fails soft (skips the bad entry, or leaves the whole map empty) rather than
crashing — every stage just falls back to `evil_battle1` in that case.

---

## 8. Known unknowns — deliberately not guessed at

These remain open. Do not assign confident names to any of them without new
reverse-engineering evidence; treat anything below as a starting point for
future investigation, not a settled fact.

```text
soundData + 24 / +32 / +40   — special player roles. +24 shares battlePlayer's
                                 exact vtable (CONFIRMED, same class). +32 is
                                 armed with a cue directly during Werehog setup
                                 itself, sourced from a static string constant
                                 at guest address 0x8203282C — the actual text
                                 of that string has not been read yet. That's
                                 the strongest lead so far toward identifying
                                 a Titan/boss-specific player.

Player-creation r6 argument  — value 0 for normalPlayer, 3 for all four other
                                 slots at creation time. Not confirmed to be a
                                 "resource selector" or similar — don't name it
                                 without tracing.

soundData + 168 / + 169      — battle-state bytes read by a MIDASM hook
                                 (WerehogBattleMusicMidAsmHook). Setters are
                                 known (sub_82B45C58/68/78/88), but none of
                                 them are confirmed to mean "in battle" —
                                 testing showed the derived value is
                                 transition-related, not a persistent combat
                                 flag. Do not use it as a battle-state check.

Titan/boss music regression  — an earlier persistent-battle prototype caused
                                 Titan music to disappear. Root cause still
                                 UNKNOWN. Worth re-testing now that the final
                                 architecture is stable, since it may no
                                 longer reproduce, but hasn't been re-tested
                                 specifically against Titan encounters yet.

True per-stage bank/archive  — the current system keeps every custom remix in
redirection                    one shared bgm_stg_e_btl bank, selected by cue
                                 name in code. A true per-stage bank swap
                                 (smaller shared bank, one archive per stage)
                                 would require finding and hooking whatever
                                 native code resolves the logical bank name to
                                 a physical archive file — not yet located.
                                 Not necessary unless the shared bank's size
                                 becomes a real problem.

Multi-mod distribution/merge — JSON multi-mod merging and .csb/.cpk audio-bank
                                 merging across mods were investigated and
                                 designed but intentionally deferred until the
                                 mod is ready to ship. See 00_README_FIRST.md's
                                 "Distribution/merging investigation" section
                                 for the full design if/when this is picked
                                 back up.
```

---

## 9. Final implementation (misc_patches.cpp — Werehog section)

This is the complete, current, tested state of the synchronization system.
Diagnostic logging has been trimmed to the events that are actually useful
to see in a log (state transitions, suppressions, the stage-restart signal)
— the old per-call, unconditional prints that fired for every player in the
game regardless of relevance have been removed.

```cpp
static uint32_t g_werehogNormalPlayer = 0;
static uint32_t g_werehogBattlePlayer = 0;
static uint32_t g_werehogSpecialPlayer = 0;

static bool g_werehogBattlePrimed = false;
static bool g_werehogPersistentBattleStarted = false;
static bool g_insideWerehogBattleEntry = false;

static bool g_werehogEarlyPairPrepared = false;
static bool g_werehogWaitingForNativeNormalCatchup = false;

// --- Werehog audio setup: capture player identities, reset lifecycle state ---
PPC_FUNC_IMPL(__imp__sub_82B48548);

PPC_FUNC(sub_82B48548)
{
    uint32_t owner = ctx.r3.u32;

    __imp__sub_82B48548(ctx, base);

    if (owner == 0)
        return;

    uint32_t soundData = PPC_LOAD_U32(owner + 156);
    if (soundData == 0)
        return;

    g_werehogNormalPlayer = PPC_LOAD_U32(soundData + 8);
    g_werehogBattlePlayer = PPC_LOAD_U32(soundData + 16);
    g_werehogSpecialPlayer = PPC_LOAD_U32(soundData + 24);

    g_werehogBattlePrimed = false;
    g_werehogPersistentBattleStarted = false;
    g_werehogEarlyPairPrepared = false;
    g_werehogWaitingForNativeNormalCatchup = false;
}

// --- D528: player activation. The synchronization point. ---
PPC_FUNC_IMPL(__imp__sub_82B4D778);
PPC_FUNC_IMPL(__imp__sub_82B4D528);

PPC_FUNC(sub_82B4D528)
{
    uint32_t player = ctx.r3.u32;
    uint32_t value = ctx.r4.u32;

    if (player == 0)
    {
        __imp__sub_82B4D528(ctx, base);
        return;
    }

    // EARLY-BATTLE catchup: we already force-started normalPlayer ourselves
    // below (EARLY PAIR START). Swallow native's own later, redundant call
    // exactly once so normalPlayer isn't restarted from 0:00.
    if (player == g_werehogNormalPlayer && value == 1 && g_werehogWaitingForNativeNormalCatchup)
    {
        g_werehogWaitingForNativeNormalCatchup = false;
        return;
    }

    // EARLY-BATTLE case: battle entry is trying to activate battlePlayer
    // before normalPlayer has ever started (encounter triggered immediately
    // on stage load). Force-start both together so their timelines match.
    if (player == g_werehogBattlePlayer && value == 1 && g_werehogEarlyPairPrepared && !g_werehogPersistentBattleStarted)
    {
        PPCContext normalCtx = ctx;
        normalCtx.r3.u32 = g_werehogNormalPlayer;
        normalCtx.r4.u32 = 1;

        __imp__sub_82B4D528(normalCtx, base);
        __imp__sub_82B4D528(ctx, base);

        g_werehogPersistentBattleStarted = true;
        g_werehogEarlyPairPrepared = false;
        g_werehogWaitingForNativeNormalCatchup = true;
        return;
    }

    // battlePlayer is already persistently running. Suppress native's
    // re-activation so it doesn't restart from 0:00 on every subsequent battle.
    if (player == g_werehogBattlePlayer && value == 1 && g_werehogPersistentBattleStarted)
        return;

    if (player != g_werehogNormalPlayer || value != 1)
    {
        __imp__sub_82B4D528(ctx, base);
        return;
    }

    // NORMAL-FIRST case: normalPlayer activates for real (always allowed
    // through). If battlePlayer hasn't started yet, start it immediately
    // afterward so both timelines begin together.
    __imp__sub_82B4D528(ctx, base);

    if (!g_werehogPersistentBattleStarted)
    {
        PPCContext battleCtx = ctx;
        battleCtx.r3.u32 = g_werehogBattlePlayer;
        battleCtx.r4.u32 = 1;

        __imp__sub_82B4D528(battleCtx, base);
        g_werehogPersistentBattleStarted = true;
    }
}

// --- D970: cue arming. ---
PPC_FUNC_IMPL(__imp__sub_82B4D970);

PPC_FUNC(sub_82B4D970)
{
    if (ctx.r4.u32 == 0)
    {
        __imp__sub_82B4D970(ctx, base);
        return;
    }

    const char* cueName = (const char*)(base + ctx.r4.u32);
    uint32_t player = ctx.r3.u32;

    // EARLY-BATTLE catchup: native tries to re-arm evil_normal on
    // normalPlayer after we already force-started it early. Ignore — it's
    // already primed and playing.
    if (player == g_werehogNormalPlayer &&
        strcmp(cueName, "evil_normal") == 0 &&
        g_werehogWaitingForNativeNormalCatchup)
    {
        return;
    }

    // EARLY-BATTLE case: battle entry is arming battlePlayer's cue before
    // normalPlayer has ever been primed. Arm both cues together here so
    // sub_82B4D528's EARLY PAIR START branch can start them in sync.
    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && !g_werehogBattlePrimed)
    {
        PPCContext normalCtx = ctx;
        normalCtx.r3.u32 = g_werehogNormalPlayer;
        normalCtx.r4.u32 = GetCueGuestAddress("evil_normal");

        PPCContext battleCtx = ctx;
        battleCtx.r3.u32 = g_werehogBattlePlayer;
        battleCtx.r4.u32 = GetCueGuestAddress(GetStageBattleCueName());

        __imp__sub_82B4D970(normalCtx, base);
        __imp__sub_82B4D970(battleCtx, base);

        g_werehogBattlePrimed = true;
        g_werehogEarlyPairPrepared = true;
        return;
    }

    // Persistent battlePlayer is already running and battle entry is trying
    // to re-arm its cue. Suppressed — the persistent cue must survive re-encounters.
    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && g_werehogPersistentBattleStarted)
        return;

    // NORMAL-FIRST case: evil_normal is being armed on normalPlayer for the
    // first time. Arm the battle cue on battlePlayer at the same lifecycle
    // point so sub_82B4D528 can start both together later.
    if (strcmp(cueName, "evil_normal") == 0 && g_werehogBattlePlayer != 0 && !g_werehogBattlePrimed)
    {
        PPCContext battleCtx = ctx;
        battleCtx.r3.u32 = g_werehogBattlePlayer;
        battleCtx.r4.u32 = GetCueGuestAddress(GetStageBattleCueName());

        __imp__sub_82B4D970(ctx, base);
        __imp__sub_82B4D970(battleCtx, base);

        g_werehogBattlePrimed = true;
    }
    else
    {
        __imp__sub_82B4D970(ctx, base);
    }
}

// --- Battle-entry scope tracking, used by the D970/D528/D778 filters above ---
PPC_FUNC_IMPL(__imp__sub_82B465C8);

PPC_FUNC(sub_82B465C8)
{
    g_insideWerehogBattleEntry = true;
    __imp__sub_82B465C8(ctx, base);
    g_insideWerehogBattleEntry = false;
}

// --- D778: player reset. Suppress during battle entry; detect stage restart otherwise. ---
PPC_FUNC(sub_82B4D778)
{
    uint32_t player = ctx.r3.u32;

    // Battle entry while our persistent battlePlayer is alive: don't let
    // the game reset it.
    if (player == g_werehogBattlePlayer &&
        g_insideWerehogBattleEntry &&
        g_werehogPersistentBattleStarted)
    {
        return;
    }

    __imp__sub_82B4D778(ctx, base);

    // battlePlayer reset OUTSIDE battle entry while we still think it's
    // persistently alive means something external invalidated it (stage
    // restart reusing the same player object). Rebuild the pair from scratch
    // next lifecycle.
    if (player == g_werehogBattlePlayer &&
        !g_insideWerehogBattleEntry &&
        g_werehogPersistentBattleStarted)
    {
        g_werehogBattlePrimed = false;
        g_werehogPersistentBattleStarted = false;
        g_werehogEarlyPairPrepared = false;
        g_werehogWaitingForNativeNormalCatchup = false;
    }
}
```

Two housekeeping notes if you're diffing this against an older copy of
`misc_patches.cpp`:

- The `sub_82B45C78` wrapper (a two-line diagnostic tracer for the
  `soundData+168/169` investigation) has been removed — it had no
  functional effect, only logging. Re-add it if you resume that specific
  investigation (Section 8).
- A stray commented-out `//static bool g_werehogBattleNeedsResync = false;`
  and a duplicate commented-out `PPC_FUNC_IMPL(__imp__sub_82B4D778);`
  declaration have been removed as dead leftovers from earlier iteration.

---

## 10. Maintenance going forward

Given the system is confirmed stable, expected future work is content-only:

- **New stage remixes:** follow Section 7. No code changes needed.
- **Titan/special-player investigation:** optional, separate effort — see
  Section 8. Not required for the core system to keep working.
- **Distribution/multi-mod merging:** deferred by design until the mod is
  ready to ship — see `00_README_FIRST.md`'s distribution section for the
  agreed-but-unimplemented plan.
- **If/when official code-modding support ships for this game:** the
  synchronization logic here doesn't depend on anything specific to the
  recompiler beyond the guest/host memory model and the `PPC_FUNC`/`__imp__`
  wrapper convention (Section 5). Porting the same state machine (Section 6)
  to a different modding API should be straightforward — the four flags and
  the three lifecycle scenarios are the part that took real design work; the
  wrapper mechanics around them are replaceable.

---

## 11. Evidence hierarchy

When anything in a future investigation seems to contradict this document:

```text
1. Generated/native PPC control flow (ground truth for mechanics)
2. Direct in-game test results (ground truth for behavior)
3. This document / current source
4. Runtime log observations
5. Hypotheses
```

Mechanical PPC evidence tells you what code *does*; it does not by itself
tell you what an unnamed field or function *means*. Keep that distinction
when extending Section 8.
