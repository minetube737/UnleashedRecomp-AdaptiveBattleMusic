#include <api/SWA.h>
#include <ui/game_window.h>
#include <user/achievement_manager.h>
#include <user/persistent_storage_manager.h>
#include <user/config.h>
#include <kernel/heap.h>

static uint32_t g_werehogNormalPlayer = 0;
static uint32_t g_werehogBattlePlayer = 0;

static bool g_werehogBattlePrimed = false;
static bool g_insideWerehogBattleEntry = false;

static bool g_werehogPersistentBattleStarted = false;

struct CreatedSoundPlayer
{
    uint32_t player;
    uint32_t arg4;
    uint32_t arg5;
    uint32_t arg6;
    uint32_t arg7;
};

static CreatedSoundPlayer g_createdPlayers[256]{};
static int g_createdPlayerCount = 0;

void AchievementManagerUnlockMidAsmHook(PPCRegister& id)
{
    AchievementManager::Unlock(id.u32);
}

bool DisableHintsMidAsmHook()
{
    return !Config::Hints;
}

// Disable Perfect Dark Gaia hints.
PPC_FUNC_IMPL(__imp__sub_82AC36E0);
PPC_FUNC(sub_82AC36E0)
{
    auto pPerfectDarkGaiaChipHintName = (xpointer<char>*)g_memory.Translate(0x8338EF10);

    strcpy(pPerfectDarkGaiaChipHintName->get(), Config::Hints ? "V_CHP_067\0" : "end\0");

    __imp__sub_82AC36E0(ctx, base);
}

bool DisableControlTutorialMidAsmHook()
{
    return !Config::ControlTutorial;
}

bool DisableEvilControlTutorialMidAsmHook(PPCRegister& r4, PPCRegister& r5)
{
    if (Config::ControlTutorial)
        return true;

    // Only allow enemy QTE prompts to get through.
    return r4.u32 == 1 && r5.u32 == 1;
}

bool DisableDLCIconMidAsmHook()
{
    return Config::DisableDLCIcon;
}
//WEREHOG BATTLE MUSIC START
uint32_t ReadGuestU32(uint32_t address)
{
    uint8_t* base = g_memory.base;
    return PPC_LOAD_U32(address);
}

static float ReadGuestF32(
    uint8_t* base,
    uint32_t address)
{
    PPCRegister value{};

    value.u32 =
        PPC_LOAD_U32(address);

    return value.f32;
}

void DumpWerehogPlayerInternals(
    const char* name,
    uint32_t player)
{
    if (player == 0)
    {
        printf(
            "%s:\n"
            "  player = NULL\n",
            name
        );

        return;
    }

    uint32_t internal =
        ReadGuestU32(player + 4);

    printf(
        "%s:\n"
        "  player           = 0x%08X\n"
        "  internal         = 0x%08X\n",
        name,
        player,
        internal
    );

    if (internal == 0)
        return;

    uint32_t sharedContext =
        ReadGuestU32(internal + 0);

    uint32_t lowerObject =
        ReadGuestU32(internal + 4);

    uint32_t controlCandidate =
        ReadGuestU32(internal + 8);

    printf(
        "  internal+0      = 0x%08X\n"
        "  internal+4      = 0x%08X  [lower object]\n"
        "  internal+8      = 0x%08X  [control candidate]\n",
        sharedContext,
        lowerObject,
        controlCandidate
    );

    if (lowerObject != 0)
    {
        printf(
            "  LOWER OBJECT:\n"
            "    +0  = 0x%08X\n"
            "    +4  = 0x%08X\n"
            "    +8  = 0x%08X\n"
            "    +12 = 0x%08X\n"
            "    +16 = 0x%08X\n",
            ReadGuestU32(lowerObject + 0),
            ReadGuestU32(lowerObject + 4),
            ReadGuestU32(lowerObject + 8),
            ReadGuestU32(lowerObject + 12),
            ReadGuestU32(lowerObject + 16)
        );
    }
}


void DumpWerehogAudioStrings()
{
    constexpr uint32_t addresses[] =
    {
        0x82032818,
        0x8203282C,
        0x82032838,
        0x82032848,
        0x820C4C24
    };

    printf("\n=== WEREHOG AUDIO STRINGS ===\n");

    for (uint32_t address : addresses)
    {
        const char* text =
            reinterpret_cast<const char*>(
                g_memory.Translate(address));

        printf(
            "0x%08X = %s\n",
            address,
            text);
    }

    printf("==============================\n");
}

void WerehogBattleMusicMidAsmHook(PPCRegister& r11)
{
    static uint8_t lastState = 0xFF;

    uint8_t state = r11.u8;
    
    static bool printedWerehogStrings = false;
    //TEMP
    if (!printedWerehogStrings)
    {
        printedWerehogStrings = true;

        const char* stringA =
            (const char*)g_memory.Translate(0x820C4794);

        const char* stringB =
            (const char*)g_memory.Translate(0x820C4770);

        printf("0x820C4794 = %s\n", stringA);
        printf("0x820C4770 = %s\n", stringB);
    }


    if (state != lastState)
    {
        lastState = state;

        auto pGameDocument = SWA::CGameDocument::GetInstance();

        if (pGameDocument && pGameDocument->m_pMember)
        {
            const char* stageName =
                pGameDocument->m_pMember->m_StageName.c_str();

            if (state == 4)
            {
                if (strcmp(stageName, "ActN_MykonosEvil") == 0)
                {
                    printf(
                        "Windmill Isle Werehog battle started! Stage: %s\n",
                        stageName
                    );
                }
                /*else
                {
                printf(
                    "Werehog battle started! Stage: %s\n",
                    stageName
                );*/
            }
            else if (state == 3)
            {
                printf(
                    "Werehog battle ended! Stage: %s\n",
                    stageName
                );
            }
        }
    }

    if (Config::BattleTheme)
        return;

    // Swap CStateBattle for CStateNormal.
    if (r11.u8 == 4)
        r11.u8 = 3;
}

void WerehogBattleCueTestMidAsmHook(PPCRegister& r4)
{
    //uint8_t* base = g_memory.base;

    const char* cueName =
        reinterpret_cast<const char*>(g_memory.Translate(r4.u32));

    const char* customBattleCue = "test_battle";

    size_t customBattleCueSize = strlen(customBattleCue) + 1;

    static void* customBattleCueMemory = g_userHeap.Alloc(customBattleCueSize);

    memcpy(customBattleCueMemory, customBattleCue, customBattleCueSize);

    static uint32_t customBattleCueAddress = g_memory.MapVirtual(customBattleCueMemory);

    const char* translatedCustomBattleCue =
        reinterpret_cast<const char*>(g_memory.Translate(customBattleCueAddress));

    printf("%s\n", translatedCustomBattleCue);

    //g_userHeap.Free(customBattleCueMemory);

    printf("Werehog battle cue: %s\n", cueName);

    //constexpr uint32_t battleCueTable = 0x83306A5C;

    //r4.u32 = PPC_LOAD_U32(battleCueTable + (translatedCustomBattleCue));
    r4.u32 = customBattleCueAddress;

    const char* cuePrefix =
        reinterpret_cast<const char*>(g_memory.Translate(0x820C54E8));

    printf("Cue prefix: %s\n", cuePrefix);

    const char* newCue =
        reinterpret_cast<const char*>(g_memory.Translate(r4.u32));

    printf("Changed Werehog battle cue to: %s\n", newCue);
}

uint32_t GetTestBattleCueAddress()
{
    static uint32_t address = 0;

    if (address == 0)
    {
        const char* cue = "test_battle";
        size_t size = strlen(cue) + 1;

        void* memory = g_userHeap.Alloc(size);

        memcpy(
            memory,
            cue,
            size
        );

        address =
            g_memory.MapVirtual(memory);
    }

    return address;
}

static bool CallPlayerVtable8(
    PPCContext ctx,
    uint8_t* base,
    uint32_t player,
    uint32_t value)
{
    if (player == 0)
        return false;

    uint32_t vtable =
        PPC_LOAD_U32(player + 0);

    if (vtable == 0)
        return false;

    uint32_t method =
        PPC_LOAD_U32(vtable + 8);

    if (method == 0)
        return false;

    printf(
        "Player vtable+8 call:\n"
        "  player = 0x%08X\n"
        "  vtable = 0x%08X\n"
        "  method = 0x%08X\n"
        "  value  = %u\n",
        player,
        vtable,
        method,
        value
    );

    ctx.r3.u64 = player;
    ctx.r4.u64 = value;

    PPC_CALL_INDIRECT_FUNC(method);

    return true;
}

// ============================================
// WEREHOG PERSISTENT BATTLE TRANSITION
// ============================================

PPC_FUNC_IMPL(__imp__sub_82B478E0);

PPC_FUNC(sub_82B478E0)
{
    uint32_t transition =
        ctx.r3.u32;

    /*
     * Only interfere after we have successfully started
     * our persistent battle-music instance.
     */
    if (g_werehogPersistentBattleStarted && transition != 0 && PPC_LOAD_U8(transition + 100) == 0)
    {
        /*
         * Native code calls sub_82E627B8 and compares
         * its returned f1 against transition +104.
         *
         * We reproduce that condition so we intercept
         * at the SAME point the game would've called
         * battlePlayer->vtable+8(true).
         */
        PPCContext timeCtx = ctx;

        timeCtx.r3.u64 =
            transition;

        sub_82E627B8(timeCtx, base);

        float currentTime =
            (float)timeCtx.f1.f64;

        float startTime =
            ReadGuestF32(base, transition + 104);

        if (startTime <= currentTime)
        {
            /*
             * Reproduce:
             *
             * sub_8315CF68(...)
             * soundData = *(result + 156)
             */
            PPCContext ownerCtx = ctx;

            ownerCtx.r3.u64 =
                transition;

            sub_8315CF68(ownerCtx, base);

            uint32_t owner =
                ownerCtx.r3.u32;

            uint32_t soundData = 0;

            if (owner != 0)
            {
                soundData =
                    PPC_LOAD_U32(owner + 156);
            }

            if (soundData != 0)
            {
                uint32_t battlePlayer =
                    PPC_LOAD_U32(soundData + 16);

                /*
                 * Safety check:
                 * only suppress the call if this really
                 * is OUR Werehog battlePlayer.
                 */
                if (battlePlayer == g_werehogBattlePlayer)
                {
                    float value108 =
                        ReadGuestF32(base, transition + 108);

                    float value112 =
                        ReadGuestF32(base, transition + 112);

                    /*
                     * Native code reads this exact
                     * game constant for f1.
                     */
                    float nativeOne =
                        ReadGuestF32(base, 0x820008C4);

                    /*
                     * Reproduce the native fade setup
                     * on soundData +224.
                     *
                     * What we intentionally DO NOT
                     * reproduce is:
                     *
                     * battlePlayer->vtable+8(true)
                     */

                    PPCContext fadeCtx = ctx;

                    fadeCtx.r3.u64 =
                        soundData + 224;

                    fadeCtx.f1.f64 =
                        value108;

                    sub_82B4E1F0(fadeCtx, base);

                    fadeCtx = ctx;

                    fadeCtx.r3.u64 =
                        soundData + 224;

                    fadeCtx.f1.f64 =
                        nativeOne;

                    fadeCtx.f2.f64 =
                        value112;

                    sub_82B4E190(fadeCtx, base);

                    /*
                     * Tell this transition object:
                     *
                     * "yes, the battle-player start
                     * phase has already happened."
                     *
                     * A new transition object gets
                     * created for a later encounter,
                     * so this is NOT a global game
                     * state mutation.
                     */
                    PPC_STORE_U8(transition + 100, 1);

                    printf("Suppressed native battlePlayer " "restart; keeping persistent " "test_battle instance\n");

                    /*
                     * IMPORTANT:
                     *
                     * Do not call the original this frame.
                     *
                     * Native code itself jumps directly
                     * to the end after performing this
                     * activation/fade block.
                     *
                     * Calling the original after setting
                     * +100 would make it execute the
                     * NEXT phase one frame too early.
                     */
                    return;
                }
            }
        }
    }

    __imp__sub_82B478E0(ctx, base);
}

/*Used to help reverse engineer the battle music cue system*/
PPC_FUNC_IMPL(__imp__sub_82B4D970);

PPC_FUNC(sub_82B4D970)
{
    const char* cueName =
        (const char*)(base + ctx.r4.u32);

    if (g_insideWerehogBattleEntry && g_werehogPersistentBattleStarted && ctx.r3.u32 == g_werehogBattlePlayer)
    {
        printf(
            "Skipping battle-entry SetCue(%s)\n",
            cueName ? cueName : "<null>"
        );

        return;
    }

    if (cueName != nullptr)
    {
        if (
            g_werehogBattlePlayer != 0 &&
            !g_werehogBattlePrimed)
        {
            PPCContext battleCtx = ctx;

            battleCtx.r3.u64 =
                g_werehogBattlePlayer;

            battleCtx.r4.u64 =
                GetTestBattleCueAddress();

            printf(
                "Selecting test_battle on battlePlayer "
                "0x%08X\n",
                g_werehogBattlePlayer
            );

            // Step 1:
            // Select/prepare the cue.
            __imp__sub_82B4D970(
                battleCtx,
                base
            );

            g_werehogBattlePrimed = true;

            // Step 2:
            // Do what the native game normally does later
            // when it wants the selected cue to actually run.
            if (CallPlayerVtable8(
                ctx,
                base,
                g_werehogBattlePlayer,
                1))
            {
                g_werehogPersistentBattleStarted = true;

                printf(
                    "Persistent test_battle playback started "
                    "with volume still controlled by the game\n"
                );
            }
        }
        else if (
            strcmp(cueName, "evil_battle1") == 0 ||
            strcmp(cueName, "evil_battle2") == 0 ||
            strcmp(cueName, "evil_battle3") == 0 ||
            strcmp(cueName, "evil_battle4") == 0)
        {
            g_werehogBattlePlayer = ctx.r3.u32;

            printf(
                "Werehog BGM: cue=%s player=0x%08X\n",
                cueName,
                ctx.r3.u32
            );

            for (int i = g_createdPlayerCount - 1; i >= 0; i--)
            {
                if (g_createdPlayers[i].player == ctx.r3.u32)
                {
                    printf(
                        "MATCHED BATTLE PLAYER CREATION:\n"
                        "  index = %d\n"
                        "  player = 0x%08X\n"
                        "  r4 = 0x%08X\n"
                        "  r5 = %u\n"
                        "  r6 = %u\n"
                        "  r7 = %u\n",
                        i,
                        g_createdPlayers[i].player,
                        g_createdPlayers[i].arg4,
                        g_createdPlayers[i].arg5,
                        g_createdPlayers[i].arg6,
                        g_createdPlayers[i].arg7
                    );

                    break;
                }
            }
        }
    }

    __imp__sub_82B4D970(ctx, base);
}

/*Used to track volume changes for werehog BGM*/
PPC_FUNC_IMPL(__imp__sub_82B4DA70);

PPC_FUNC(sub_82B4DA70)
{
    uint32_t player = ctx.r3.u32;
    float volume = (float)ctx.f1.f64;

    static int lastNormalBucket = -1;
    static int lastBattleBucket = -1;

    auto getBucket = [](float value)
        {
            if (value < 0.01f)
                return 0; // muted

            if (value < 0.25f)
                return 1;

            if (value < 0.75f)
                return 2;

            return 3; // mostly/full volume
        };

    if (player == g_werehogNormalPlayer)
    {
        int bucket = getBucket(volume);

        if (bucket != lastNormalBucket)
        {
            lastNormalBucket = bucket;

            printf(
                "NORMAL volume: %.3f\n",
                volume
            );
        }
    }
    else if (player == g_werehogBattlePlayer)
    {
        int bucket = getBucket(volume);

        if (bucket != lastBattleBucket)
        {
            lastBattleBucket = bucket;

            printf(
                "BATTLE volume: %.3f\n",
                volume
            );
        }
    }

    __imp__sub_82B4DA70(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_82B465C8);

PPC_FUNC(sub_82B465C8)
{
    g_insideWerehogBattleEntry = true;

    __imp__sub_82B465C8(
        ctx,
        base
    );

    g_insideWerehogBattleEntry = false;
}

/*Used to check if the evil_normal is muted yet plays under bgm_stg_e_btl*/
PPC_FUNC_IMPL(__imp__sub_82B4D778);

PPC_FUNC(sub_82B4D778)
{
    uint32_t player = ctx.r3.u32;

    if (player == g_werehogNormalPlayer)
    {
        printf(
            "D778 called on NORMAL player 0x%08X\n",
            player
        );
    }
    else if (player == g_werehogBattlePlayer)
    {
        printf(
            "D778 called on BATTLE player 0x%08X\n",
            player
        );
    }

    // Preserve our already-running persistent battle timeline.
    // Only suppress D778 when it is the battle player,
    // during the normal battle-entry routine,
    // and our persistent instance has actually been started.
    if (
        player == g_werehogBattlePlayer &&
        g_insideWerehogBattleEntry &&
        g_werehogPersistentBattleStarted)
    {
        printf(
            "Skipping battlePlayer D778 during battle entry: "
            "persistent instance must survive\n"
        );

        return;
    }

    __imp__sub_82B4D778(ctx, base);
}

/*more battle music reverse engineering diagnostics*/
PPC_FUNC_IMPL(__imp__sub_82B4DF50);

PPC_FUNC(sub_82B4DF50)
{
    uint32_t outputAddress = ctx.r3.u32;

    uint32_t arg4 = ctx.r4.u32;
    uint32_t arg5 = ctx.r5.u32;
    uint32_t arg6 = ctx.r6.u32;
    uint32_t arg7 = ctx.r7.u32;

    __imp__sub_82B4DF50(ctx, base);

    uint32_t player =
        PPC_LOAD_U32(outputAddress);

    if (player != 0 && g_createdPlayerCount < 256)
    {
        auto& record =
            g_createdPlayers[g_createdPlayerCount++];

        record.player = player;
        record.arg4 = arg4;
        record.arg5 = arg5;
        record.arg6 = arg6;
        record.arg7 = arg7;
    }
}
/*Werehog BGM SETUP*/
PPC_FUNC_IMPL(__imp__sub_82B48548);

PPC_FUNC(sub_82B48548)
{
    static int callCount = 0;

    // Save this before calling the original function because
    // the function is free to modify ctx.r3.
    uint32_t ownerAddress = ctx.r3.u32;

    printf(
        "\n=== Werehog BGM SETUP #%d ===\n",
        ++callCount
    );

    // Run the game's original Werehog BGM setup.
    __imp__sub_82B48548(ctx, base);

    // owner +156 contains the Werehog sound data object.
    uint32_t member =
        PPC_LOAD_U32(ownerAddress + 156);

    if (member != 0)
    {
        // These are the actual player objects we've already identified.
        uint32_t normalPlayer =
            PPC_LOAD_U32(member + 8);

        uint32_t battlePlayer =
            PPC_LOAD_U32(member + 16);

        g_werehogNormalPlayer = normalPlayer;
        g_werehogBattlePlayer = battlePlayer;

        g_werehogBattlePrimed = false;
        g_werehogPersistentBattleStarted = false;
        printf(
            "normal +8  = 0x%08X\n"
            "battle +16 = 0x%08X\n"
            "==========================\n",
            normalPlayer,
            battlePlayer
        );

        printf(
            "\n=== WEREHOG PLAYER INTERNALS ===\n"
        );

        DumpWerehogPlayerInternals(
            "NORMAL",
            normalPlayer
        );

        DumpWerehogPlayerInternals(
            "BATTLE",
            battlePlayer
        );

        printf(
            "=================================\n"
        );
    }

    DumpWerehogAudioStrings();
}

PPC_FUNC_IMPL(__imp__sub_82B4D870);

PPC_FUNC(sub_82B4D870)
{
    uint32_t player = ctx.r3.u32;
    uint32_t value = ctx.r4.u32;

    if (player == g_werehogNormalPlayer)
    {
        printf(
            "D870 NORMAL player=0x%08X value=%u\n",
            player,
            value
        );
    }
    else if (player == g_werehogBattlePlayer)
    {
        printf(
            "D870 BATTLE player=0x%08X value=%u\n",
            player,
            value
        );
    }

    __imp__sub_82B4D870(
        ctx,
        base
    );
}

PPC_FUNC_IMPL(__imp__sub_82B44AA8);

PPC_FUNC(sub_82B44AA8)
{
    printf(
        "AA8: entering battle-player activation candidate\n"
    );

    uint32_t owner = ctx.r3.u32;

    uint32_t soundData =
        PPC_LOAD_U32(owner + 156);

    if (soundData != 0)
    {
        uint32_t battlePlayer =
            PPC_LOAD_U32(soundData + 16);

        printf(
            "AA8: battlePlayer=0x%08X\n",
            battlePlayer
        );
    }

    __imp__sub_82B44AA8(
        ctx,
        base
    );
}

//WEREHOG BATTLE MUSIC END
bool UseAlternateTitleMidAsmHook()
{
    auto isSWA = Config::Language == ELanguage::Japanese;

    if (Config::UseAlternateTitle)
        isSWA = !isSWA;

    return isSWA;
}

/* Hook function that gets the game region
   and force result to zero for Japanese
   to display the correct logos. */
PPC_FUNC_IMPL(__imp__sub_825197C0);
PPC_FUNC(sub_825197C0)
{
    if (Config::Language == ELanguage::Japanese)
    {
        ctx.r3.u64 = 0;
        return;
    }

    __imp__sub_825197C0(ctx, base);
}

// Logo skip
PPC_FUNC_IMPL(__imp__sub_82547DF0);
PPC_FUNC(sub_82547DF0)
{
    if (Config::SkipIntroLogos)
    {
        ctx.r4.u64 = 0;
        ctx.r5.u64 = 0;
        ctx.r6.u64 = 1;
        ctx.r7.u64 = 0;
        sub_825517C8(ctx, base);
    }
    else
    {
        __imp__sub_82547DF0(ctx, base);
    }
}

/* Ignore xercesc::EmptyStackException to
   allow DLC stages with invalid XML to load. */
PPC_FUNC_IMPL(__imp__sub_8305D5B8);
PPC_FUNC(sub_8305D5B8)
{
    auto value = PPC_LOAD_U32(ctx.r3.u32 + 4);

    if (!value)
        return;

    __imp__sub_8305D5B8(ctx, base);
}

// Disable auto save warning.
PPC_FUNC_IMPL(__imp__sub_82586698);
PPC_FUNC(sub_82586698)
{
    if (Config::DisableAutoSaveWarning)
        *(bool*)g_memory.Translate(0x83367BC2) = true;

    __imp__sub_82586698(ctx, base);
}

// SWA::CObjHint::MsgNotifyObjectEvent::Impl
// Disable only certain hints from hint volumes.
// This hook should be used to allow hint volumes specifically to also prevent them from affecting the player.
PPC_FUNC_IMPL(__imp__sub_82736E80);
PPC_FUNC(sub_82736E80)
{
    // GroupID parameter text
    auto* groupId = (const char*)(base + PPC_LOAD_U32(ctx.r3.u32 + 0x100));
    
    if (!Config::Hints)
    {
        // WhiteIsland_ACT1_001: "Your friend went off that way, Sonic. Quick, let's go after him!"
        // s20n_mykETF_c_navi_2: "Huh? Weird! We can't get through here anymore. We were able to earlier!"
        if (strcmp(groupId, "WhiteIsland_ACT1_001") != 0 && strcmp(groupId, "s20n_mykETF_c_navi_2") != 0)
            return;
    }

    __imp__sub_82736E80(ctx, base);
}

// SWA::CHelpWindow::MsgRequestHelp::Impl
// Disable only certain hints from other sequences.
// This hook should be used to block hint messages from unknown sources.
PPC_FUNC_IMPL(__imp__sub_824C1E60);
PPC_FUNC(sub_824C1E60)
{
    auto pMsgRequestHelp = (SWA::Message::MsgRequestHelp*)(base + ctx.r4.u32);

    if (!Config::Hints)
    {
        // s10d_mykETF_c_navi: "Looks like we can get to a bunch of places in the village from here!"
        if (strcmp(pMsgRequestHelp->m_Name.c_str(), "s10d_mykETF_c_navi") == 0)
            return;
    }

    __imp__sub_824C1E60(ctx, base);
}

// This function is called in various places but primarily for the boost filter
// when the second argument (r4) is set to "boost". Whilst boosting the third argument (f1)
// will go up to 1.0f and then down to 0.0f as the player lets off of the boost button.
// To avoid the boost filter from kicking in at all if the function is called with "boost"
// we set the third argument to zero no matter what (if the code is on).
PPC_FUNC_IMPL(__imp__sub_82B4DB48);
PPC_FUNC(sub_82B4DB48)
{
    if (Config::DisableBoostFilter && strcmp((const char*)(base + ctx.r4.u32), "boost") == 0)
    {
        ctx.f1.f64 = 0.0;
    }

    __imp__sub_82B4DB48(ctx, base);
}

// DLC save data flag check.
// 
// The DLC checks are fundamentally broken in this game, resulting in this method always
// returning true and displaying the DLC info message when it shouldn't be.
// 
// The original intent here seems to have been to display the message every time new DLC
// content is installed, but the flags in the save data never get written to properly,
// causing this function to always pass in some way.
//
// We bypass the save data completely and write to external persistent storage to store
// whether we've seen the DLC info message instead. This way we can retain the original
// broken game behaviour, whilst also providing a fix for this issue that is safe.
PPC_FUNC_IMPL(__imp__sub_824EE620);
PPC_FUNC(sub_824EE620)
{
    __imp__sub_824EE620(ctx, base);

    ctx.r3.u32 = PersistentStorageManager::ShouldDisplayDLCMessage(true);
}
