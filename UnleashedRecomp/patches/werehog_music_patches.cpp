#include <api/SWA.h>
#include <user/config.h>
#include <kernel/heap.h>
#include <unordered_map>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include <mod/mod_loader.h>

static uint32_t g_werehogNormalPlayer = 0;
static uint32_t g_werehogBattlePlayer = 0;

static bool g_insideWerehogBattleEntry = false;

enum class WerehogMusicState
{
    Idle,
    PreparedFromNormal,
    PreparedFromBattle,
    WaitingForNativeNormal,
    Running
};

static WerehogMusicState g_werehogMusicState = WerehogMusicState::Idle;

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

static std::unordered_map<std::string, std::string> g_stageBattleCues;

static void LoadStageBattleCues()
{
    // First look through enabled mods using Unleashed Recompiled's
    // existing mod loader.
    auto path = ModLoader::ResolvePath("stage_battle_cues.json");

    if (path.empty())
    {
        printf("stage_battle_cues.json was not found in any enabled mod.\n");
        return;
    }

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
    printf("[STAGE NAME] %s\n", stageName);

    //auto it = g_stageBattleCues.find(stageName);
    //if (it != g_stageBattleCues.end())
    //    return it->second.c_str();

    for (const auto& [location, cue] : g_stageBattleCues)
    {
        if (std::string(stageName).find(location) != std::string::npos)
            return cue.c_str();
    }

    return "evil_battle1";
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

    g_werehogMusicState = WerehogMusicState::Idle;
}

static bool IsWerehogMusicPersistent()
{
    return g_werehogMusicState == WerehogMusicState::Running || g_werehogMusicState == WerehogMusicState::WaitingForNativeNormal;
}

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

    if (player == g_werehogNormalPlayer && value == 1 && g_werehogMusicState == WerehogMusicState::WaitingForNativeNormal)
    {
        printf("[EARLY PAIR] Ignoring late native NORMAL start\n");

        g_werehogMusicState = WerehogMusicState::Running;
        return;
    }

    if (player == g_werehogBattlePlayer && value == 1 && g_werehogMusicState == WerehogMusicState::PreparedFromBattle)
    {
        printf("[EARLY PAIR START] Starting NORMAL + BATTLE together\n");

        PPCContext normalCtx = ctx;
        normalCtx.r3.u32 = g_werehogNormalPlayer;
        normalCtx.r4.u32 = 1;

        __imp__sub_82B4D528(normalCtx, base);
        __imp__sub_82B4D528(ctx, base);

        g_werehogMusicState = WerehogMusicState::WaitingForNativeNormal;
        return;
    }

    // battlePlayer is already persistently running (past the first
    // encounter). Suppress native's re-activation so it doesn't restart
    // from 0:00 on every subsequent battle.
    if (player == g_werehogBattlePlayer && value == 1 && IsWerehogMusicPersistent())
    {
        return;
    }

    if (player != g_werehogNormalPlayer || value != 1)
    {
        __imp__sub_82B4D528(ctx, base);
        return;
    }

    PPCContext battleCtx = ctx;
    battleCtx.r3.u32 = g_werehogBattlePlayer;
    battleCtx.r4.u32 = 1;

    __imp__sub_82B4D528(ctx, base);

    if (g_werehogMusicState == WerehogMusicState::PreparedFromNormal)
    {
        __imp__sub_82B4D528(battleCtx, base);
        g_werehogMusicState = WerehogMusicState::Running;
    }
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

    if (player == g_werehogNormalPlayer && strcmp(cueName, "evil_normal") == 0 && g_werehogMusicState == WerehogMusicState::WaitingForNativeNormal)
    {
        printf("[EARLY PAIR] Ignoring late native NORMAL cue setup\n");
        return;
    }

    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && g_werehogMusicState == WerehogMusicState::Idle)
    {
        printf("[EARLY BATTLE] Preparing synchronized NORMAL + BATTLE pair\n");

        // Prepare evil_normal on normalPlayer.
        PPCContext normalCtx = ctx;
        normalCtx.r3.u32 = g_werehogNormalPlayer;
        normalCtx.r4.u32 = GetCueGuestAddress("evil_normal");

        // Prepare custom battle cue on battlePlayer.
        PPCContext battleCtx = ctx;
        battleCtx.r3.u32 = g_werehogBattlePlayer;
        battleCtx.r4.u32 = GetCueGuestAddress(GetStageBattleCueName());

        __imp__sub_82B4D970(normalCtx, base);
        __imp__sub_82B4D970(battleCtx, base);

        g_werehogMusicState = WerehogMusicState::PreparedFromBattle;

        return;
    }

    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && IsWerehogMusicPersistent())
    {
        return;
    }

    if (strcmp(cueName, "evil_normal") == 0 && g_werehogBattlePlayer != 0 && g_werehogMusicState == WerehogMusicState::Idle)
    {
        printf("[NORMAL CUE SETUP REQUEST]\n");

        uint32_t BattleCueAddress = GetCueGuestAddress(GetStageBattleCueName());
        printf("[ADAPTIVE CUE] %s\n", GetStageBattleCueName());

        PPCContext battleCtx = ctx;
        battleCtx.r3.u64 = g_werehogBattlePlayer;
        battleCtx.r4.u64 = BattleCueAddress;
        __imp__sub_82B4D970(ctx, base);
        __imp__sub_82B4D970(battleCtx, base);
        g_werehogMusicState = WerehogMusicState::PreparedFromNormal;
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

    __imp__sub_82B465C8(ctx, base);

    g_insideWerehogBattleEntry = false;
}

PPC_FUNC(sub_82B4D778)
{
    uint32_t player = ctx.r3.u32;

    // Normal battle entry while our persistent battlePlayer is alive:
    // DON'T let the game reset it.
    if (player == g_werehogBattlePlayer && g_insideWerehogBattleEntry && IsWerehogMusicPersistent())
    {
        printf(
            "Skipping battlePlayer D778 during battle entry: "
            "persistent instance must survive\n"
        );

        return;
    }

    // Let the game's D778 happen normally.
    __imp__sub_82B4D778(ctx, base);

    // If battlePlayer gets reset OUTSIDE battle entry while our
    // persistent system thinks it's alive, its playback state is
    // no longer trustworthy.
    //
    // This is what appears to happen during Stage Restart.
    if (player == g_werehogBattlePlayer && !g_insideWerehogBattleEntry && IsWerehogMusicPersistent())
    {
        printf("[PERSISTENT RESET] battlePlayer reset outside battle entry\n");

        g_werehogMusicState = WerehogMusicState::Idle;
    }
}
