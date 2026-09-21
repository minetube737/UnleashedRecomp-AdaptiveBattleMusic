#include <api/SWA.h>
#include <ui/game_window.h>
#include <user/achievement_manager.h>
#include <user/persistent_storage_manager.h>
#include <user/config.h>
#include <kernel/heap.h>
#include <unordered_map>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include <user/paths.h>

static uint32_t g_werehogNormalPlayer = 0;
static uint32_t g_werehogBattlePlayer = 0;
static uint32_t g_werehogSpecialPlayer = 0;

static bool g_werehogBattlePrimed = false;
static bool g_werehogPersistentBattleStarted = false;
static bool g_insideWerehogBattleEntry = false;

static bool g_werehogBattleNeedsResync = false;

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

void WerehogBattleMusicMidAsmHook(PPCRegister& r11)
{

    if (Config::BattleTheme)
        return;

    // Swap CStateBattle for CStateNormal.
    if (r11.u8 == 4)
        r11.u8 = 3;
}

static uint32_t GetCueGuestAddress(const char* cue)
{
    static std::unordered_map<std::string, uint32_t> cache;

    auto it = cache.find(cue);
    if (it != cache.end())
    {
        return it->second;
    }

    size_t size = strlen(cue) + 1;
    void* memory = g_userHeap.Alloc(size);
    memcpy(memory, cue, size);
    uint32_t address = g_memory.MapVirtual(memory);

    cache.emplace(cue, address);
    return address;
}

static std::unordered_map<std::string, std::string> g_stageBattleCues = {
    /*{"ActN_MykonosEvil", "myk_e_btl"},
    { "ActN_Mission_Mykonos", "myk_e_btl" },
    { "ActN_SubMykonos_01", "myk_e_btl" },
    { "ActN_SubMykonos_02", "myk_e_btl" },
    { "ActN_SubMykonos_03", "myk_e_btl" },
    { "ActN_SubMykonos_04", "myk_e_btl" },
    { "ActN_BeachEvil", "sea_e_btl" },
    { "ActN_Mission_Beach", "sea_e_btl" },
    { "ActN_SubBeach_01", "sea_e_btl" },
    { "ActN_SubBeach_02", "sea_e_btl" },
    { "ActN_SubBeach_03", "sea_e_btl" },
    { "ActN_SnowEvil", "snw_e_btl" },
    { "ActN_Mission_Snow", "snw_e_btl" },
    { "ActN_SubSnow_01", "snw_e_btl" },
    { "ActN_SubSnow_02", "snw_e_btl" },
    // add more stage IDs -> cue names as you author them*/
};

static void LoadStageBattleCues()
{
    auto path = GetGamePath() / "stage_battle_cues.json";

    printf("Looking for stage cue file at: %s\n", path.string().c_str());

    std::ifstream stream(path);


    if (!stream.is_open())
    {
        printf("Stage cue file not found.\n");
        return;
    }

    nlohmann::json j;

    try
    {
        j = nlohmann::json::parse(stream);
    }
    catch (nlohmann::json::parse_error& err)
    {
        printf("Failed to parse stage_battle_cues.json: %s\n", err.what());
        return;
    }

    for (auto& [key, value] : j.items())
    {
        try
        {
            g_stageBattleCues[key] = value.get<std::string>();
        }
        catch (nlohmann::json::type_error& err)
        {
            printf("Failed to parse value for stage '%s' in stage_battle_cues.json: %s\n", key.c_str(), err.what());
        }
    }

    printf("Loaded %zu stage battle cue entries from stage_battle_cues.json.\n", g_stageBattleCues.size());
}

static const char* GetStageBattleCueName()
{
    static bool loaded = false;
    if (!loaded)
    {
        LoadStageBattleCues();
        loaded = true;
    }

    auto pGameDocument = SWA::CGameDocument::GetInstance();
    if (!pGameDocument)
        return "evil_battle1";

    const char* stageName = pGameDocument->m_pMember->m_StageName.c_str();

    auto it = g_stageBattleCues.find(stageName);
    if (it != g_stageBattleCues.end())
        return it->second.c_str();

    return "evil_battle1";
}



void WerehogBattleCueTestMidAsmHook(PPCRegister& r4)
{   
    static uint32_t customBattleCueAddress = 0;
    const char* customBattleCue = "test_battle";


    size_t customBattleCueSize = strlen(customBattleCue) + 1;
    static void* customBattleCueMemory = g_userHeap.Alloc(customBattleCueSize);
    
    if (customBattleCueAddress == 0)
    {
        memcpy(customBattleCueMemory, customBattleCue, customBattleCueSize);
        customBattleCueAddress = g_memory.MapVirtual(customBattleCueMemory);
    }

    r4.u32 = customBattleCueAddress;
}

PPC_FUNC_IMPL(__imp__sub_82B48548);

PPC_FUNC(sub_82B48548)
{
    uint32_t owner = ctx.r3.u32;

    __imp__sub_82B48548(ctx, base);

    if (owner == 0)
    {
        return;
    }

    uint32_t soundData = PPC_LOAD_U32(owner + 156);
    if (soundData == 0)
    {
        return;
    }

    g_werehogNormalPlayer = PPC_LOAD_U32(soundData + 8);
    g_werehogBattlePlayer = PPC_LOAD_U32(soundData + 16);
    g_werehogSpecialPlayer = PPC_LOAD_U32(soundData + 24);
    g_werehogBattlePrimed = false;
    g_werehogPersistentBattleStarted = false;
    g_werehogBattleNeedsResync = false;
}

PPC_FUNC_IMPL(__imp__sub_82B4D778);

PPC_FUNC_IMPL(__imp__sub_82B4D528);

PPC_FUNC(sub_82B4D528)
{
    uint32_t player = ctx.r3.u32;
    uint32_t value = ctx.r4.u32;

    const char* playerLabel = (player == g_werehogNormalPlayer) ? "NORMAL" : (player == g_werehogBattlePlayer) ? "BATTLE" : "OTHER";

    printf("[D528] player=%s (0x%08X) value=%u persistentStarted=%d insideBattleEntry=%d\n",
        playerLabel, player, value, g_werehogPersistentBattleStarted, g_insideWerehogBattleEntry);

    if (player == 0)
    {
        __imp__sub_82B4D528(ctx, base);
        return;
    }

    // NEW: catch any attempt to re-activate a battle player
    // that's already persistently running, and block it.
    if (player == g_werehogBattlePlayer && value == 1 && g_werehogPersistentBattleStarted)
    {
        return; // no call to original — this activation is suppressed
    }

    if (player != g_werehogNormalPlayer || value != 1)
    {
        __imp__sub_82B4D528(ctx, base);
        return;
    }

    if (g_werehogBattleNeedsResync)
    {
        printf("[RESYNC] Resetting early battlePlayer before persistent setup\n");

        PPCContext resetCtx = ctx;
        resetCtx.r3.u32 = g_werehogBattlePlayer;


        __imp__sub_82B4D778(resetCtx, base);

        g_werehogBattleNeedsResync = false;
    }

    PPCContext battleCtx = ctx;
    battleCtx.r3.u32 = g_werehogBattlePlayer;
    battleCtx.r4.u32 = 1;
    __imp__sub_82B4D528(ctx, base);
    __imp__sub_82B4D528(battleCtx, base);

    g_werehogPersistentBattleStarted = true;
}

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

    const char* playerLabel = (player == g_werehogNormalPlayer) ? "NORMAL" : (player == g_werehogBattlePlayer) ? "BATTLE" : "OTHER";

    printf("[D970] player=%s (0x%08X) cue=%s primed=%d persistentStarted=%d insideBattleEntry=%d\n", playerLabel, player, cueName, g_werehogBattlePrimed, g_werehogPersistentBattleStarted, g_insideWerehogBattleEntry);
    
    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && !g_werehogBattlePrimed)
    {
        printf("[EARLY BATTLE] Using custom battle cue\n");

        ctx.r4.u32 = GetCueGuestAddress(GetStageBattleCueName());

        __imp__sub_82B4D970(ctx, base);

        g_werehogBattleNeedsResync = true;

        return;
    }

    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && g_werehogPersistentBattleStarted)
    {
        return;
    }

    if(strcmp(cueName, "evil_normal") == 0 && g_werehogBattlePlayer != 0 && !g_werehogBattlePrimed)
    {
        printf("[NORMAL CUE SETUP REQUEST]\n");

        /*if (g_werehogBattleNeedsResync)
        {
            printf("[RESYNC] Resetting early battlePlayer before persistent setup\n");

            PPCContext resetCtx = ctx;
            resetCtx.r3.u32 = g_werehogBattlePlayer;


            __imp__sub_82B4D778(resetCtx, base);

            g_werehogBattleNeedsResync = false;
        }*/


        uint32_t testBattleCueAddress = GetCueGuestAddress(GetStageBattleCueName());

        PPCContext battleCtx = ctx;
        battleCtx.r3.u64 = g_werehogBattlePlayer;
        battleCtx.r4.u64 = testBattleCueAddress;
        __imp__sub_82B4D970(ctx, base);
        __imp__sub_82B4D970(battleCtx, base);
        g_werehogBattlePrimed = true;
    }
    else
    {
        __imp__sub_82B4D970(ctx, base);
    }
}

PPC_FUNC_IMPL(__imp__sub_82B465C8);

PPC_FUNC(sub_82B465C8)
{
    g_insideWerehogBattleEntry = true;

    printf("[465C8] battle entry START\n");

    __imp__sub_82B465C8(ctx, base);

    printf("[465C8] battle entry END\n");

    g_insideWerehogBattleEntry = false;
}

PPC_FUNC_IMPL(__imp__sub_82B45C78);

PPC_FUNC(sub_82B45C78)
{
    uint32_t owner = ctx.r3.u32;
    uint32_t soundData = PPC_LOAD_U32(owner + 156);

    printf("D45C78 reached for Werehog owner 0x%08X\n", owner);
    printf("D45C78 reached for Werehog soundData 0x%08X\n", soundData);

    __imp__sub_82B45C78(ctx, base);
}

//PPC_FUNC_IMPL(__imp__sub_82B4D778);

PPC_FUNC(sub_82B4D778)
{
    uint32_t player = ctx.r3.u32;
    const char* playerLabel = (player == g_werehogNormalPlayer) ? "NORMAL" : (player == g_werehogBattlePlayer) ? "BATTLE" : "OTHER";

    printf("[D778] player=%s (0x%08X) persistentStarted=%d insideBattleEntry=%d\n", playerLabel, player, g_werehogPersistentBattleStarted, g_insideWerehogBattleEntry);

    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && g_werehogPersistentBattleStarted)
    {
        printf(
            "Skipping battlePlayer D778 during battle entry: "
            "persistent instance must survive\n"
        );

        return;
    }
    
    __imp__sub_82B4D778(ctx, base);

}



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
