#include "common.h"


GameInfo g_gameInfo[2];

/* The string fields are 4-byte handles, which can't be static initializers. */
__attribute__((constructor)) static void GameSettings_InitGameInfo(void) {
    static const char *const strings[2][5] = {
        { "Hot Wheels(TM)   Velocity X", "HotWheels.elf", "hwvx", "hwvx", "hw.ico" },
        { "Dooms Day Rescue", "SLUS_001.00", "DoomsDayRescue", "DoomsDayRescue", "ddr.ico" },
    };
    s32 i;

    for (i = 0; i < 2; i++) {
        SETP(g_gameInfo[i].name, strings[i][0]);
        SETP(g_gameInfo[i].id, strings[i][1]);
        SETP(g_gameInfo[i].title, strings[i][2]);
        SETP(g_gameInfo[i].fileName, strings[i][3]);
        SETP(g_gameInfo[i].icon, strings[i][4]);
    }
}

char g_saveFileName[11];
char g_fileNames[8][41];
u8 g_DDRsavedGameData[0xE83C];
s8 g_DDRescueGameData[0x1CDC];
u8 g_HWsavedGameData[0x8FC];
HotWheelsGameData g_HotWheelsGameData;
GeneralGameSettings g_generalGameSettings;
CodeVariables g_codeVariables;
extern s8 g_bValidFileName[8];
extern s8 g_bRefreshMC;
extern s8 g_bRefreshMC_FileList;
extern s8 g_bRefreshMC_FileInfo;

static s8 bFirstTime = 1;
static MCP *pMCP;


static inline MCP *GameSettings_LoadMCP(void)
{
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    void *pack;

    if (bFirstTime)
    {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL)
        {
            return NULL;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    return pMCP;
}

typedef struct DDRescueGameData {
    u8 unk0[0x6C];
    s32 progressionID;
    s32 progression;
    s8 bValid;
    u8 unk75[0x78 - 0x75];
    u8 playerStats[0x38];
    u8 actions[0x880 - 0xB0];
    f32 unk880[50];
    u8 unk948[50][2];
    f32 unk9AC[50];
    u8 unkA74[50][2];
    u8 unkAD8[200];
    u8 unkBA0[1000];
    u8 unkF88[2000];
    u8 unk1758[500];
    u8 unk194C[50];
    f32 unk1980[50][3];
    f32 unk1BD8[50];
    u8 unk1CA0[50];
    s32 vehicle;
    s32 item;
} DDRescueGameData;


static inline u32 GameSettings_GetSaveFileChecksum(u32 device, const char *name, u32 size)
{
    extern u8 *g_pSavedGameData;
    u8 *data = g_pSavedGameData;
    s32 handle;

    if ((handle = File_Open(device + 0xA, name, 0, NULL)) != -1)
    {
        File_Read(handle, size * 8 + 0x15C, data);
        File_Close(handle);
        return ((u32 *)data)[2];
    }
    return -1;
}

void GameSetting_Init(void) {
    extern GameInfo g_gameInfo[2];
    extern s8 g_bGameSettingsInitialized;
    extern u8 *g_pGameData;
    extern u8 *g_pSavedGameData;
    u32 i;

    g_bGameSettingsInitialized = 1;
    memset(&g_codeVariables, 0, sizeof(CodeVariables));
    g_codeVariables.device = 1;
    GameSettings_SetDefaultControllerState();
    GameSettings_ResetPlayerVehicleItem();
    GameSettings_ResetGame();
    memset(&g_generalGameSettings, 0, sizeof(GeneralGameSettings));
    g_generalGameSettings.difficulty = 0;
    g_generalGameSettings.audio.speakerMode = OSGetSoundMode();
    g_generalGameSettings.audio.musicVolume = 100;
    g_generalGameSettings.audio.sfxVolume = 100;
    for (i = 1; i <= 4; i++) {
        GameSettings_ResetControllerConfig(i);
        g_generalGameSettings.controllers[i - 1].data[0] = 1;
    }
    switch (GameSettings_GetGameType()) {
    case 0:
        g_gameInfo[0].unk14 = 0x8F0;
        g_gameInfo[0].unk18 = 0xF4;
        g_pGameData = (u8 *)&g_HotWheelsGameData;
        g_pSavedGameData = g_HWsavedGameData;
        memset(&g_HotWheelsGameData, 0, sizeof(HotWheelsGameData));
        g_HotWheelsGameData.cars[3] = 1;
        g_HotWheelsGameData.cars[4] = 1;
        g_HotWheelsGameData.cars[5] = 1;
        g_HotWheelsGameData.unk93[0] = 1;
        g_HotWheelsGameData.unk93[1] = 1;
        g_HotWheelsGameData.unk93[2] = 1;
        g_HotWheelsGameData.tracks[0] = 1;
        g_HotWheelsGameData.tracks[5] = 1;
        g_HotWheelsGameData.tracks[0x13] = 1;
        g_HotWheelsGameData.tracks[0x14] = 1;
        g_HotWheelsGameData.tracks[0x15] = 1;
        g_HotWheelsGameData.tracks[0x16] = 1;
        g_HotWheelsGameData.tracks[0x1D] = 1;
        g_HotWheelsGameData.tracks[0x1E] = 1;
        g_HotWheelsGameData.tracks[0x1F] = 1;
        g_HotWheelsGameData.tracks[0x20] = 1;
        g_HotWheelsGameData.tracks[0x27] = 1;
        g_HotWheelsGameData.unkAA[0] = 1;
        g_HotWheelsGameData.unkAA[1] = 1;
        g_HotWheelsGameData.unkAA[4] = 1;
        break;
    case 1:
        g_gameInfo[1].unk14 = 0xE830;
        g_gameInfo[1].unk18 = 0x1CDC;
        g_pGameData = (u8 *)g_DDRescueGameData;
        g_pSavedGameData = g_DDRsavedGameData;
        memset(g_DDRescueGameData, 0, sizeof(g_DDRescueGameData));
        g_DDRescueGameData[0] = 1;
        g_DDRescueGameData[0x14] = 1;
        GameSettings_SetCodeVariable(0x6A, 0xA, 1);
        GameSettings_SetCodeVariable(0x6B, 0xA, 1);
        break;
    }
}

void GameSettings_ResetGame(void)
{
    s32 i;

    g_codeVariables.unk30 = 1;
    for (i = 0; i < 4; i++)
    {
        g_codeVariables.unk34[i] = 0;
        g_codeVariables.unk44[i] = 0;
        g_codeVariables.unk54[i] = 0;
        g_codeVariables.unk64[i] = 0;
    }
    g_codeVariables.unkE4 = 0;
    g_codeVariables.unkE8 = 0;
    g_codeVariables.unkEC = 300000;
    g_codeVariables.unkF0 = 10;
    g_codeVariables.unkF4 = 1000;
    g_codeVariables.unkF8 = 3;
    g_codeVariables.unkFC = 0;
    g_codeVariables.unk100 = 3;
    g_codeVariables.unk104 = 1;
    g_codeVariables.unk108 = 1;
    g_codeVariables.unk111 = 1;
}

void GameSettings_SetDefaultControllerState(void)
{
    s32 i;

    for (i = 0; i < 4; i++)
    {
        g_codeVariables.controllerState[i] = 1;
    }
}

void GameSettings_ResetPlayerVehicleItem(void) {
    M2C_FIELD(&g_codeVariables, s32 *, 0) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 0x10) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 4) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 0x14) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 8) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 0x18) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 0xC) = -1;
    M2C_FIELD(&g_codeVariables, s32 *, 0x1C) = -1;
}

u8 GameSettings_GetGameType(void)
{
    MCP *mcp;

    mcp = GameSettings_GetMCP();
    if (mcp != NULL)
    {
        return mcp->gameType;
    }
    return 0;
}

MCP *GameSettings_GetMCP(void)
{
    void *pack;

    if (bFirstTime)
    {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL)
        {
            return NULL;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    return pMCP;
}


void GameSettings_ResetControllerConfig(s32 player)
{
    MCP *mcp;
    void *pack;
    ControllerSettings *settings;
    u8 *src;

    if (bFirstTime)
    {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL)
        {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    settings = GameSettings_GetControllerSettings(player);
    if (mcp == NULL || mcp->numBindings == 0)
    {
        memset(&settings->data[2], 0, 0x10);
    }
    else
    {
        src = GETP(MCP_BINDING(mcp, 0)->settings);
        memcpy(&settings->data[2], src, 0x10);
    }
}

/* port: [(s32)arg - 1] indexes -1 for arg 0, as the original's 32-bit address
 * arithmetic wrapped; with an unsigned index it would land 16 GB away. */
void GameSettings_SetCodeVariable(s32 var, u32 arg, u32 val) {
    extern s8 g_bGameSettingsInitialized;
    extern s8 g_bBlockReset;
    s32 type;
    MCP *mcp;

    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        type = mcp->gameType;
    } else {
        type = 0;
    }
    if (!g_bGameSettingsInitialized) {
        GameSetting_Init();
    }

    switch (var) {
    case 0x64:
        switch (type) {
        case 0:
            if (arg < 0x21) {
                g_HotWheelsGameData.cars[arg] = val != 0;
            }
            break;
        case 1:
            if (arg < 0x14) {
                g_DDRescueGameData[arg] = val != 0;
            }
            break;
        }
        break;
    case 0x65:
        switch (type) {
        case 0:
            if (arg < 0x39) {
                g_HotWheelsGameData.tracks[arg] = val != 0;
            }
            break;
        case 1:
            if (arg < 0xA) {
                g_DDRescueGameData[0x14 + arg] = val != 0;
            }
            break;
        }
        break;
    case 0x66:
        switch (type) {
        case 0:
            if (arg < 0xC) {
                g_HotWheelsGameData.unk93[arg] = val != 0;
            }
            break;
        }
        break;
    case 0x68:
        switch (type) {
        case 0:
            if (arg < 5) {
                g_HotWheelsGameData.unkAA[arg] = val != 0;
            }
            break;
        case 1:
            if (arg < 5) {
                g_DDRescueGameData[0x29 + arg] = val != 0;
            }
            break;
        }
        break;
    case 0x69:
        switch (type) {
        case 0:
            g_HotWheelsGameData.unk9F[9] = val != 0;
            break;
        case 1:
            g_DDRescueGameData[0x27] = val != 0;
            break;
        }
        break;
    case 0x6A:
        switch (type) {
        case 0:
            if (arg < 0xB) {
                g_HotWheelsGameData.unk9F[arg] = val != 0;
            }
            break;
        case 1:
            if (arg < 0xB) {
                g_DDRescueGameData[0x1E + arg] = val != 0;
            }
            break;
        }
        break;
    case 0x6B:
        if (arg < 0xB) {
            g_codeVariables.unk128[arg] = val != 0;
        }
        break;
    case 0x6C:
        if (arg < 0xB) {
            g_codeVariables.unk128[arg] = val;
        }
        break;
    case 0xC8:
        g_generalGameSettings.difficulty = val;
        break;
    case 0xC9:
        if (arg >= 1 && arg <= 10) {
            g_codeVariables.controllerState[(s32)arg - 1] = val;
        }
        break;
    case 0xCA:
        switch (type) {
        case 0:
            if (arg < 0x1E) {
                g_HotWheelsGameData.unkB0[arg] = val;
            }
            break;
        case 1:
            if (arg < 0x1E) {
                ((u16 *)&g_DDRescueGameData[0x2E])[arg] = val;
            }
            break;
        }
        break;
    case 0xCB:
        g_codeVariables.playerVehicle[(s32)arg - 1] = val;
        if (arg - 1 == 0) {
            switch (type) {
            case 0:
                g_HotWheelsGameData.unkEC = val;
                break;
            case 1:
                *(s32 *)&g_DDRescueGameData[0x1CD4] = val;
                break;
            }
        }
        break;
    case 0xCC:
        g_codeVariables.playerItem[(s32)arg - 1] = val;
        if (arg - 1 == 0) {
            switch (type) {
            case 0:
                *(s32 *)g_HotWheelsGameData.padF0 = val;
                break;
            case 1:
                *(s32 *)&g_DDRescueGameData[0x1CD8] = val;
                break;
            }
        }
        break;
    case 0xCE:
        if (arg <= 4) {
            g_generalGameSettings.controllers[(s32)arg - 1].data[1] = val;
            GameSettings_ApplyDefaultGameData();
        }
        break;
    case 0xD1:
        if (arg <= 4) {
            g_generalGameSettings.controllers[(s32)arg - 1].data[0] = val;
            GameSettings_ApplyDefaultGameData();
            if (val) {
                Input_SetForceFeedbackActive(arg - 1, 1);
                Input_SetForceFeedback(arg - 1, 0, 1.0f, 5.0f);
            }
        }
        break;
    case 0xCF:
        if (val > 100) {
            val = 100;
        }
        g_generalGameSettings.audio.musicVolume = val;
        GameSettings_ApplyDefaultGameData();
        break;
    case 0xD0:
        if (val > 100) {
            val = 100;
        }
        g_generalGameSettings.audio.sfxVolume = val;
        GameSettings_ApplyDefaultGameData();
        break;
    case 0xD6:
        if (val > 100) {
            val = 100;
        }
        g_generalGameSettings.audio.speakerMode = val;
        GameSettings_ApplyDefaultGameData();
        break;
    case 0xD5:
        g_codeVariables.unkE0 = val;
        break;
    case 0x12C:
        g_codeVariables.unkE4 = val != 0;
        break;
    case 0x12D:
        g_codeVariables.unkE8 = val;
        break;
    case 0x12E:
        g_codeVariables.unkEC = val * 300000;
        break;
    case 0x12F:
        g_codeVariables.unkF0 = val;
        break;
    case 0x130:
        g_codeVariables.unkF4 = val * 1000;
        break;
    case 0x131:
        g_codeVariables.unkF8 = val;
        break;
    case 0x132:
        g_codeVariables.unkFC = val != 0;
        break;
    case 0x133:
        g_codeVariables.unk100 = val;
        break;
    case 0x134:
        g_codeVariables.unk104 = val;
        break;
    case 0x136:
        g_codeVariables.unk108 = val != 0;
        break;
    case 0x190:
        g_codeVariables.unk30 = val;
        break;
    case 0x191:
        if (arg <= 4) {
            g_codeVariables.unk34[(s32)arg - 1] = val;
        }
        break;
    case 0x192:
        if (arg <= 4) {
            g_codeVariables.unk44[(s32)arg - 1] = val;
        }
        break;
    case 0x193:
        if (arg <= 4) {
            g_codeVariables.unk54[(s32)arg - 1] = val;
        }
        break;
    case 0x194:
        if (arg <= 4) {
            g_codeVariables.unk64[(s32)arg - 1] = val != 0;
        }
        break;
    case 0x197:
        g_codeVariables.unk109[0] = val != 0;
        break;
    case 0x135:
        g_codeVariables.unk109[7] = val != 0;
        break;
    case 0x198:
        g_codeVariables.unk109[1] = val != 0;
        break;
    case 0x1F7:
        g_codeVariables.device = val;  /* port: a slot number, not a pointer */
        break;
    case 0x1F8:
        g_codeVariables.fileIndex = val;
        GameSettings_DetermineIfOverwritingFile();
        break;
    case 0x1FE:
        g_codeVariables.unk120[0] = val != 0;
        g_bBlockReset = g_codeVariables.unk120[0];
        break;
    case 0x19B:
        g_codeVariables.unk111 = val != 0;
        break;
    case 0x19C:
        if (arg < 0x1E) {
            g_codeVariables.unk68[arg] = val;
        }
        break;
    }
}
/* port: [(s32)arg - 1] indexes -1 for arg 0, as the original's 32-bit address
 * arithmetic wrapped; with an unsigned index it would land 16 GB away. */
u32 GameSettings_GetCodeVariable(s32 var, u32 arg) {
    typedef struct WorldEntry {
        u32 unk0;
        PTR32(u8) thing;
    } WorldEntry;
    typedef struct World {
        u8 pad0[0x58];
        u32 numEntries;
        PTR32(WorldEntry) entries;
    } World;
    extern s8 g_bGameSettingsInitialized;
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    s32 type;
    MCP *mcp;
    void *pack;
    u32 i;
    u8 count;
    u8 *player;
    s32 plugged;
    s32 port;
    s32 n;
    s32 device;
    s32 status;
    World *world;

    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        type = mcp->gameType;
    } else {
        type = 0;
    }
    if (!g_bGameSettingsInitialized) {
        GameSetting_Init();
    }

    switch (var) {
    case 0x64:
        if (GameSettings_IsCheatEnabled(1)) {
            return 1;
        }
        switch (type) {
        case 0:
            if (arg < 0x21) {
                return g_HotWheelsGameData.cars[arg];
            }
            break;
        case 1:
            if (arg < 0x14) {
                return g_DDRescueGameData[arg];
            }
            break;
        }
        break;
    case 0x65:
        if (GameSettings_IsCheatEnabled(3)) {
            return 1;
        }
        switch (type) {
        case 0:
            if (arg < 0x39) {
                return g_HotWheelsGameData.tracks[arg];
            }
            break;
        case 1:
            if (arg < 0xA) {
                return g_DDRescueGameData[0x14 + arg];
            }
            break;
        }
        break;
    case 0x66:
        if (GameSettings_IsCheatEnabled(2)) {
            return 1;
        }
        switch (type) {
        case 0:
            if (arg < 0xC) {
                return g_HotWheelsGameData.unk93[arg];
            }
            break;
        }
        break;
    case 0x68:
        if (GameSettings_IsCheatEnabled(4)) {
            return 1;
        }
        switch (type) {
        case 0:
            if (arg < 5) {
                return g_HotWheelsGameData.unkAA[arg];
            }
            break;
        case 1:
            if (arg < 5) {
                return g_DDRescueGameData[0x29 + arg];
            }
            break;
        }
        break;
    case 0x69:
        return GameSettings_IsCheatEnabled(9);
    case 0x6A:
        switch (type) {
        case 0:
            return g_HotWheelsGameData.unk9F[arg] || g_HotWheelsGameData.unkA9;
        case 1:
            return g_DDRescueGameData[0x1E + arg] || g_DDRescueGameData[0x28];
        }
        break;
    case 0x6B:
        return GameSettings_IsCheatEnabled(arg);
    case 0x6C:
        return g_codeVariables.unk128[arg];
    case 0xC8:
        return g_generalGameSettings.difficulty;
    case 0xC9:
        if ((1 << (arg - 1)) & Input_GetContollersPluggedIn()) {
            return g_codeVariables.controllerState[(s32)arg - 1];
        }
        g_codeVariables.playerVehicle[(s32)arg - 1] = -1;
        g_codeVariables.controllerState[(s32)arg - 1] = 1;
        return 0;
    case 0xCA:
        switch (type) {
        case 0:
            if (arg < 0x1E) {
                return g_HotWheelsGameData.unkB0[arg];
            }
            break;
        case 1:
            if (arg < 0x1E) {
                return ((u16 *)g_DDRescueGameData)[0x17 + arg];
            }
            break;
        }
        break;
    case 0xCB:
        if (StateManager_CurrState() == 5) {
            u8 numPlayers = PlayerManager_PlayerCount();
            u32 index;

            for (index = 0; index < numPlayers; index++) {
                player = PlayerManager_GetPlayerByIndex(index);
                if (*(u32 *)(player + 0x60) == arg && SLOT_LOAD(u8 *, (player + 0xA8)) != NULL) {
                    return *(u32 *)(SLOT_LOAD(u8 *, (player + 0xA8)) + 0x30);
                }
            }
        }
        if (arg == 1 && g_codeVariables.unkE8 == 0) {
            switch (type) {
            case 0:
                return g_HotWheelsGameData.unkEC;
            case 1:
                return *(s32 *)&g_DDRescueGameData[0x1CD4];
            }
            break;
        }
        return g_codeVariables.playerVehicle[(s32)arg - 1];
    case 0xCC:
        return g_codeVariables.playerItem[(s32)arg - 1];
    case 0xCD:
        n = 0;
        for (port = 0; port < 4; port++) {
            if (g_codeVariables.playerVehicle[port] != -1) {
                n++;
            }
        }
        return n;
    case 0xCE:
        if (arg <= 4) {
            return g_generalGameSettings.controllers[(s32)arg - 1].data[1];
        }
        return 0;
    case 0xD1:
        if (arg <= 4) {
            return (s8)g_generalGameSettings.controllers[(s32)arg - 1].data[0];
        }
        return 0;
    case 0xD2:
        if (arg <= 4) {
            return g_codeVariables.playerVehicle[(s32)arg - 1] != -1;
        }
        return 0;
    case 0xCF:
        return g_generalGameSettings.audio.musicVolume;
    case 0xD0:
        return g_generalGameSettings.audio.sfxVolume;
    case 0xD6:
        return g_generalGameSettings.audio.speakerMode;
    case 0xD3:
        plugged = Input_GetContollersPluggedIn();
        n = 0;
        for (port = 0; port < 4; port++) {
            if ((1 << port) & plugged) {
                n++;
            }
        }
        return n;
    case 0xD4:
        plugged = Input_GetContollersPluggedIn();
        count = PlayerManager_PlayerHumanCount();
        for (i = 0; i < count; i++) {
            port = *(s32 *)(PlayerManager_GetHumanPlayerByIndex(i) + 0xC8);
            if (!(plugged & (1 << port))) {
                return port + 1;
            }
        }
        return 0;
    case 0x19A:
        world = GETP(g_worldData.world);
        if (world != NULL) {
            n = 0;
            for (port = 0; port < world->numEntries; port++) {
                if (GETP(GETP(world->entries)[port].thing)[1] == 0x17) {
                    n++;
                }
            }
            return n;
        }
        break;
    case 0xD5:
        return g_codeVariables.unkE0;
    case 0x12C:
        return g_codeVariables.unkE4;
    case 0x12D:
        return g_codeVariables.unkE8;
    case 0x12E:
        return g_codeVariables.unkEC;
    case 0x12F:
        return g_codeVariables.unkF0;
    case 0x130:
        return g_codeVariables.unkF4;
    case 0x131:
        return g_codeVariables.unkF8;
    case 0x132:
        return g_codeVariables.unkFC;
    case 0x133:
        return g_codeVariables.unk100;
    case 0x134:
        return g_codeVariables.unk104;
    case 0x136:
        return g_codeVariables.unk108;
    case 0x135:
        return g_codeVariables.unk109[7];
    case 0x190:
        return g_codeVariables.unk30;
    case 0x191:
        if (arg <= 4) {
            return g_codeVariables.unk34[(s32)arg - 1];
        }
        break;
    case 0x192:
        if (arg <= 4) {
            return g_codeVariables.unk44[(s32)arg - 1];
        }
        break;
    case 0x193:
        if (arg <= 4) {
            return g_codeVariables.unk54[(s32)arg - 1];
        }
        break;
    case 0x194:
        if (arg <= 4) {
            return g_codeVariables.unk64[(s32)arg - 1];
        }
        break;
    case 0x195:
        switch (type) {
        case 0:
            if (GameSettings_IsCheatEnabled(3)) {
                return 0xD;
            }
            for (n = 0x12; n > 4; n--) {
                if (g_HotWheelsGameData.tracks[n]) {
                    return n - 5;
                }
            }
            break;
        }
        break;
    case 0x199:
        switch (type) {
        case 0:
            if (GameSettings_IsCheatEnabled(3)) {
                return 0x11;
            }
            for (n = 0x38; n > 0x26; n--) {
                if (g_HotWheelsGameData.tracks[n]) {
                    return n - 0x27;
                }
            }
            break;
        case 1:
            break;
        }
        break;
    case 0x196:
        count = PlayerManager_PlayerCount();
        for (i = 0; i < count; i++) {
            player = PlayerManager_GetPlayerByIndex(i);
            if (*(u32 *)(player + 0x60) == arg) {
                if (Player_IsHuman(player)) {
                    return *(u32 *)(player + 0x60);
                }
                return 5;
            }
        }
        return 0;
    case 0x197:
        return g_codeVariables.unk109[0];
    case 0x198:
        return g_codeVariables.unk109[1];
    case 0x1F4:
        return GameSettings_GetDefaultMemoryCardStatus(&device, 0);
    case 0x1F5:
        GameSettings_GetDefaultMemoryCardStatus(&status, 0);
        return status;
    case 0x1F6:
        return GameSettings_GetMemoryCardStatus(arg, 0);
    case 0x1F7:
        return g_codeVariables.device;
    case 0x1F8:
        return g_codeVariables.fileIndex;
    case 0x1F9:
        return g_codeVariables.unk11C[0];
    case 0x1FA:
        return g_codeVariables.unk11C[1];
    case 0x1FB:
        return g_codeVariables.bOverwritingFile;
    case 0x1FC:
        return g_codeVariables.unk124;
    case 0x1FD:
        return g_codeVariables.unk11C[2];
    case 0x1FE:
        return g_codeVariables.unk120[0];
    case 0x258:
        return 2;
    case 0x259:
        return 1;
    case 0x19B:
        return g_codeVariables.unk111;
    case 0x19C:
        if (arg < 0x1E) {
            return g_codeVariables.unk68[arg];
        }
        break;
    }
    return 0;
}
void GameSettings_GameBoyConnected(void) {
    MCP *mcp;
    void *pack;
    s32 state;

    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        state = mcp->gameType;
    } else {
        state = 0;
    }
    switch (state) {
    case 0:
        g_HotWheelsGameData.cars[0] = 1;
        g_HotWheelsGameData.cars[1] = 1;
        g_HotWheelsGameData.cars[2] = 1;
        break;
    case 1:
        break;
    }
}

s32 GameSettings_GetDifficulty(void) {
    return g_generalGameSettings.difficulty;
}

u8 GameSettings_GetNumOfPlayers(void) {
    return PlayerManager_PlayerHumanCount();
}

s8 GameSettings_IsCheatEnabled(s32 cheat) {
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    MCP *mcp;
    void *pack;
    s32 type;
    s8 *cheats;
    u32 i;
    u8 result;

    if (Port_AllCheats()) {
        return 1;
    }
    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        type = mcp->gameType;
    } else {
        type = 0;
    }
    switch (type) {
    case 0:
        cheats = g_HotWheelsGameData.unk9F;
        break;
    case 1:
        cheats = &g_DDRescueGameData[0x1E];
        break;
    }
    if (cheat == 9) {
        for (i = 0; i < 11; i++) {
            if (i != 9 && cheats[i]) {
                return 1;
            }
        }
        if (i == 11) {
            return 0;
        }
    }
    if (((u32)(cheat - 1) <= 2 || cheat == 4) && cheats[10] && g_codeVariables.unk128[10]) {
        return 1;
    }
    result = 0;
    if ((cheats[10] || cheats != NULL) && g_codeVariables.unk128[cheat]) {
        result = 1;
    }
    return result;
}

ControllerSettings *GameSettings_GetControllerSettings(s32 player) {
    return &g_generalGameSettings.controllers[player - 1];
}

void GameSettings_GetGameInfo(void *info) {
    extern GameInfo g_gameInfo[2];
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    MCP *mcp;
    void *pack;

    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        SETP(((GameInfo *)info)->name, GETP(g_gameInfo[mcp->gameType].name));
        SETP(((GameInfo *)info)->id, GETP(g_gameInfo[mcp->gameType].id));
        SETP(((GameInfo *)info)->title, GETP(g_gameInfo[mcp->gameType].title));
        SETP(((GameInfo *)info)->fileName, GETP(g_gameInfo[mcp->gameType].fileName));
        SETP(((GameInfo *)info)->icon, GETP(g_gameInfo[mcp->gameType].icon));
        ((GameInfo *)info)->unk14 = g_gameInfo[mcp->gameType].unk14;
        ((GameInfo *)info)->unk18 = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(((GameInfo *)info)->name, "BrainCore Engine");
        SETP(((GameInfo *)info)->id, "SLUS_001.00");
        SETP(((GameInfo *)info)->title, "SavesFolder");
        SETP(((GameInfo *)info)->fileName, "saveFile");
        SETP(((GameInfo *)info)->icon, "mm.ico");
        ((GameInfo *)info)->unk14 = 0;
        ((GameInfo *)info)->unk18 = 0;
    }
}

AudioSettings *GameSettings_GetAudioSettings(void) {
    return &g_generalGameSettings.audio;
}

void GameSettings_ApplyDefaultGameData(void)
{
    SetPlayerControls();
    SetMusicGVol((u32)((f32)(g_generalGameSettings.audio.musicVolume * 0x7FFF) / 100.0f));
    SetSFXVol((u32)((f32)(g_generalGameSettings.audio.sfxVolume * 0x7FFF) / 100.0f));
    SetSpeakerMode(g_generalGameSettings.audio.speakerMode);
    OSSetSoundMode(g_generalGameSettings.audio.speakerMode);
}

void GameSettings_GetLoadedProgression(s32 *arg0, s32 *arg1)
{
    MCP *mcp;
    void *pack;
    s32 state;

    if (bFirstTime)
    {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL)
        {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL)
    {
        state = mcp->gameType;
    }
    else
    {
        state = 0;
    }
    switch (state)
    {
    case 0:
        *arg0 = 0;
        *arg1 = 0;
        break;
    case 1:
        *arg0 = *(s32 *)&g_DDRescueGameData[0x6c];
        *arg1 = *(s32 *)&g_DDRescueGameData[0x70];
        GameSettings_SetCodeVariable(0xcb, 1, *(s32 *)&g_DDRescueGameData[0x1cd4]);
        break;
    }
}

void GameSettings_RestorePlayerStats(void) {
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    MCP *mcp;
    void *pack;
    s32 state;

    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        state = mcp->gameType;
    } else {
        state = 0;
    }
    switch (state) {
    case 0:
        break;
    case 1:
        if (g_DDRescueGameData[0x74] && PlayerManager_PlayerHumanCount() == 1) {
            memcpy((u8 *)PlayerManager_GetHumanPlayerByIndex(0) + 0x70, &g_DDRescueGameData[0x78], 0x38);
        }
        break;
    }
}

void GameSettings_GatherGameData(void) {
    typedef struct DDRescueGameData {
        u8 unk0[0x6C];
        s32 progressionID;
        s32 progression;
        s8 bValid;
        u8 unk75[0x78 - 0x75];
        u8 playerStats[0x38];
        u8 actions[0x880 - 0xB0];
        f32 unk880[50];
        u8 unk948[50][2];
        f32 unk9AC[50];
        u8 unkA74[50][2];
        u8 unkAD8[200];
        u8 unkBA0[1000];
        u8 unkF88[2000];
        u8 unk1758[500];
        u8 unk194C[50];
        f32 unk1980[50][3];
        f32 unk1BD8[50];
        u8 unk1CA0[50];
        s32 vehicle;
        s32 item;
    } DDRescueGameData;
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    MCP *mcp;
    void *pack;
    s32 state;
    u8 *world;
    u8 *group;
    u8 *car;
    u32 i;
    u32 j;
    u32 nTriggers;
    u32 nSwitches;

    nTriggers = 0;
    nSwitches = 0;
    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        state = mcp->gameType;
    } else {
        state = 0;
    }
    switch (state) {
    case 0:
        break;
    case 1:
        StateProgressionManager_GetProgressionID(&((DDRescueGameData *)g_DDRescueGameData)->progressionID, &((DDRescueGameData *)g_DDRescueGameData)->progression);
        ((DDRescueGameData *)g_DDRescueGameData)->bValid = g_codeVariables.unk120[0];
        if (!((DDRescueGameData *)g_DDRescueGameData)->bValid) {
            ((DDRescueGameData *)g_DDRescueGameData)->progression++;
            break;
        }
        if (PlayerManager_PlayerHumanCount() == 1) {
            memcpy(((DDRescueGameData *)g_DDRescueGameData)->playerStats, (u8 *)PlayerManager_GetHumanPlayerByIndex(0) + 0x70, 0x38);
        }
        Trigger_SaveActions(((DDRescueGameData *)g_DDRescueGameData)->actions, 500);
        world = GETP(g_worldData.world);
        for (i = 0; i < *(u32 *)(world + 0x50) && i < 50; i++) {
            ((DDRescueGameData *)g_DDRescueGameData)->unk880[i] = *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + (i)) + 0x8);
            ((DDRescueGameData *)g_DDRescueGameData)->unk948[i][0] = (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + (i)) + 0xC)))[0];
            ((DDRescueGameData *)g_DDRescueGameData)->unk948[i][1] = (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x54)) + (i)) + 0xC)))[1];
        }
        world = GETP(g_worldData.world);
        for (i = 0; i < *(u32 *)(world + 0x48) && i < 50; i++) {
            ((DDRescueGameData *)g_DDRescueGameData)->unk9AC[i] = *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + (i)) + 0x4);
            if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + (i)) + 0xC)) != NULL) {
                ((DDRescueGameData *)g_DDRescueGameData)->unkA74[i][0] = (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + (i)) + 0xC)))[0];
                ((DDRescueGameData *)g_DDRescueGameData)->unkA74[i][1] = (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, (world + 0x4C)) + (i)) + 0xC)))[1];
            }
        }
        world = GETP(g_worldData.world);
        for (i = 0; i < *(u32 *)(world + 0x40) && i < 200; i++) {
            ((DDRescueGameData *)g_DDRescueGameData)->unkAD8[i] = *(s32 *)(SLOT_LOAD(u8 *, (world + 0x44)) + i * 0x40 + 0x20);
        }
        world = GETP(g_worldData.world);
        for (i = 0; i < *(u32 *)(world + 0x20); i++) {
            group = SLOT_LOAD(u8 *, (world + 0x24)) + i * 0x50;
            for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (group + 0x30)) + 0x14) && nTriggers < 1000; j++) {
                ((DDRescueGameData *)g_DDRescueGameData)->unkBA0[nTriggers++] = *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (group + 0x30)) + 0x18)) + j * 0x40 + 0x28);
            }
            for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (group + 0x30)) + 0x4) && nSwitches < 2000; j++) {
                ((DDRescueGameData *)g_DDRescueGameData)->unkF88[nSwitches++] = *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (group + 0x30)) + 0x8)) + j * 0x40 + 0x30);
            }
        }
        for (i = 0; i < *(u32 *)(world + 0x10) && i < 500; i++) {
            ((DDRescueGameData *)g_DDRescueGameData)->unk1758[i] = *(s32 *)(SLOT_LOAD(u8 *, (world + 0x14)) + i * 0x20 + 0x14);
        }
        world = GETP(g_worldData.world);
        for (i = 0; i < *(u32 *)(world + 0x8) && i < 50; i++) {
            ((DDRescueGameData *)g_DDRescueGameData)->unk194C[i] = *(s32 *)(SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x2C);
            car = SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x28));
            if (car != NULL) {
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][0] = *(f32 *)(car + 0x40);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][1] = *(f32 *)(car + 0x44);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][2] = *(f32 *)(car + 0x48);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1BD8[i] = (f32)*(s32 *)(car + 0x78) / (f32)*(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (car + 0x3BC)) + 0xA0)) + 0x14);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1CA0[i] = 1;
            } else {
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][0] = *(f32 *)(SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x0);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][1] = *(f32 *)(SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x4);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][2] = *(f32 *)(SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x8);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1BD8[i] = *(f32 *)(SLOT_LOAD(u8 *, (world + 0xC)) + i * 0x60 + 0x58);
                ((DDRescueGameData *)g_DDRescueGameData)->unk1CA0[i] = 0;
            }
        }
        ((DDRescueGameData *)g_DDRescueGameData)->item = 0;
        ((DDRescueGameData *)g_DDRescueGameData)->vehicle = g_codeVariables.playerVehicle[0];
        break;
    }
    g_codeVariables.unk120[0] = 0;
}

void GameSettings_RestoreGameData(void) {
    typedef struct DDRescueGameData {
        u8 unk0[0x74];
        s8 bValid;
        u8 unk75[0xB0 - 0x75];
        u8 actions[0x880 - 0xB0];
        f32 unk880[50];
        u8 unk948[50][2];
        f32 unk9AC[50];
        u8 unkA74[50][2];
        u8 unkAD8[200];
        u8 unkBA0[1000];
        u8 unkF88[2000];
        u8 unk1758[500];
        u8 unk194C[50];
        f32 unk1980[50][3];
        f32 unk1BD8[50];
        u8 unk1CA0[50];
        s32 vehicle;
        s32 item;
    } DDRescueGameData;
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    MCP *mcp;
    void *pack;
    s32 state;
    u8 *group;
    u32 i;
    u32 j;
    u32 nTriggers;
    u32 nSwitches;

    nTriggers = 0;
    nSwitches = 0;
    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    if (mcp != NULL) {
        state = mcp->gameType;
    } else {
        state = 0;
    }
    switch (state) {
    case 0:
        break;
    case 1:
        if (((DDRescueGameData *)g_DDRescueGameData)->bValid) {
            Trigger_RestoreActions(((DDRescueGameData *)g_DDRescueGameData)->actions, 500);
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x50) && i < 50; i++) {
                *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x54)) + (i)) + 0x8) = ((DDRescueGameData *)g_DDRescueGameData)->unk880[i];
                (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x54)) + (i)) + 0xC)))[0] = ((DDRescueGameData *)g_DDRescueGameData)->unk948[i][0];
                (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x54)) + (i)) + 0xC)))[1] = ((DDRescueGameData *)g_DDRescueGameData)->unk948[i][1];
            }
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x48) && i < 50; i++) {
                *(f32 *)(SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x4C)) + (i)) + 0x4) = ((DDRescueGameData *)g_DDRescueGameData)->unk9AC[i];
                if (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x4C)) + (i)) + 0xC)) != NULL) {
                    (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x4C)) + (i)) + 0xC)))[0] = ((DDRescueGameData *)g_DDRescueGameData)->unkA74[i][0];
                    (SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (u32 *)SLOT_LOAD(void *, ((u8 *)GETP(g_worldData.world) + 0x4C)) + (i)) + 0xC)))[1] = ((DDRescueGameData *)g_DDRescueGameData)->unkA74[i][1];
                }
            }
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x40) && i < 200; i++) {
                *(s32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x44)) + i * 0x40 + 0x20) = ((DDRescueGameData *)g_DDRescueGameData)->unkAD8[i];
            }
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x20); i++) {
                group = SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x24)) + i * 0x50;
                for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (group + 0x30)) + 0x14) && nTriggers < 1000; j++) {
                    *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (group + 0x30)) + 0x18)) + j * 0x40 + 0x28) = ((DDRescueGameData *)g_DDRescueGameData)->unkBA0[nTriggers++];
                }
                for (j = 0; j < *(u32 *)(SLOT_LOAD(u8 *, (group + 0x30)) + 0x4) && nSwitches < 2000; j++) {
                    *(s32 *)(SLOT_LOAD(u8 *, (SLOT_LOAD(u8 *, (group + 0x30)) + 0x8)) + j * 0x40 + 0x30) = ((DDRescueGameData *)g_DDRescueGameData)->unkF88[nSwitches++];
                }
            }
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x10) && i < 500; i++) {
                *(s32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0x14)) + i * 0x20 + 0x14) = ((DDRescueGameData *)g_DDRescueGameData)->unk1758[i];
            }
            for (i = 0; i < *(u32 *)((u8 *)GETP(g_worldData.world) + 0x8) && i < 50; i++) {
                *(s32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x2C) = ((DDRescueGameData *)g_DDRescueGameData)->unk194C[i];
                *(f32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x0) = ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][0];
                *(f32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x4) = ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][1];
                *(f32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x8) = ((DDRescueGameData *)g_DDRescueGameData)->unk1980[i][2];
                *(f32 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x58) = ((DDRescueGameData *)g_DDRescueGameData)->unk1BD8[i];
                *(u8 *)(SLOT_LOAD(u8 *, ((u8 *)GETP(g_worldData.world) + 0xC)) + i * 0x60 + 0x5F) = ((DDRescueGameData *)g_DDRescueGameData)->unk1CA0[i];
            }
            g_codeVariables.playerItem[0] = ((DDRescueGameData *)g_DDRescueGameData)->item;
            g_codeVariables.playerVehicle[0] = ((DDRescueGameData *)g_DDRescueGameData)->vehicle;
        }
        break;
    }
}


s32 GameSettings_LoadDefaultMemoryCardData(void) {
    extern u8 *g_pSavedGameData;
    s32 device;
    s32 status;
    s32 handle;
    s8 ok;
    u8 *data;
    u32 version;
    u32 size;
    GameInfo info;
    MCP *mcp;

    status = GameSettings_GetDefaultMemoryCardStatus(&device, 1);
    if (status != 1 && status != 0) {
        return 0;
    }
    device--;
    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        SETP(info.fileName, GETP(g_gameInfo[mcp->gameType].fileName));
        version = g_gameInfo[mcp->gameType].unk14;
        size = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(info.fileName, "saveFile");
        info.unk14 = 0;
        version = info.unk14;
        size = version;
    }
    data = g_pSavedGameData;
    ok = 0;
    if ((handle = File_Open(device + 0xB, GETP(info.fileName), 0, NULL)) != -1) {
        File_Read(handle, size * 8 + 0x15C, data);
        if (((u32 *)data)[0] == 4 && ((u32 *)data)[1] == version &&
            ((u32 *)data)[2] == FileManager_GetChecksum(version, data + 0xC)) {
            ok = 1;
        }
        File_Close(handle);
    }
    if (!ok) {
        return 0;
    }
    memcpy(&g_generalGameSettings, g_pSavedGameData + 0xC, 0x150);
    GameSettings_ApplyDefaultGameData();
    return 1;
}


s32 GameSettings_GetDefaultMemoryCardStatus(s32 *device, s8 bForce)
{
    static u32 s_updateCntr = 10;
    static s32 s_curStatus;
    static s32 s_curDevice;
    s32 status1;
    s32 status2;

    if (++s_updateCntr < 10 && !bForce && !g_bRefreshMC)
    {
        *device = s_curDevice;
        return s_curStatus;
    }
    s_updateCntr = 0;
    s_curStatus = 2;
    g_bRefreshMC = 0;
    status1 = GameSettings_GetMemoryCardStatus(1, 1);
    status2 = GameSettings_GetMemoryCardStatus(2, 1);
    if (status1 == 1 || status1 == 0)
    {
        s_curStatus = status1;
        s_curDevice = 1;
    }
    else if (status1 == 2 && status2 != 2)
    {
        s_curStatus = status2;
        s_curDevice = 2;
    }
    else
    {
        s_curStatus = status1;
        s_curDevice = 1;
    }
    *device = s_curDevice;
    return s_curStatus;
}




s32 GameSettings_GetMemoryCardStatus(s32 device, s8 bForce) {
    typedef struct SaveFile {
        u32 device;
        PTR32(u8) data;
        PTR32(const char) name;
        u32 version;
        u32 size;
    } SaveFile;
    extern u8 *g_pSavedGameData;
    static u32 s_curCount;
    static s32 s_curStatus;
    static s32 s_curActiveDevice;  /* port: a device number, not a pointer */
    static u32 s_curMemStatus = 0xFFFFFFFF;
    s32 status;
    s32 prevDevice;
    s32 handle;
    MCP *mcp;
    s8 ok;
    SaveFile file;
    u32 prevMemStatus;
    u64 freeBytes;
    u64 totalBytes;

    status = 2;
    if (++s_curCount >= 30) {
        s_curCount = 0;
    } else if (!g_bRefreshMC && !bForce && s_curActiveDevice == device + 10) {
        return s_curStatus;
    }
    prevDevice = s_curActiveDevice;
    s_curActiveDevice = device + 10;
    g_bRefreshMC = 0;
    do {
        prevMemStatus = s_curMemStatus;
        s_curMemStatus = FileManager_GetDeviceStatus(device + 10);
        if (s_curMemStatus == 1) {
            status = 1;
            if (prevMemStatus == s_curMemStatus && !g_bRefreshMC_FileList && !bForce && prevDevice == device + 10) {
                return s_curStatus;
            }
            mcp = GameSettings_LoadMCP();
            if (mcp != NULL) {
                SETP(file.name, GETP(g_gameInfo[mcp->gameType].fileName));
                file.version = g_gameInfo[mcp->gameType].unk14;
                file.size = g_gameInfo[mcp->gameType].unk18;
            } else {
                SETP(file.name, "saveFile");
                file.version = 0;
                file.size = file.version;
            }
            if (!FileManager_IsDeviceFormatted(device + 10)) {
                status = 4;
            } else {
                SETP(file.data, g_pSavedGameData);
                ok = 0;
                if ((handle = File_Open(device + 10, GETP(file.name), 0, NULL)) != -1) {
                    File_Read(handle, file.size * 8 + 0x15C, GETP(file.data));
                    if (((u32 *)GETP(file.data))[0] == 4 && ((u32 *)GETP(file.data))[1] == file.version &&
                        ((u32 *)GETP(file.data))[2] == FileManager_GetChecksum(file.version, GETP(file.data) + 0xC)) {
                        ok = 1;
                    }
                    File_Close(handle);
                }
                if (ok) {
                    if (!((s8 *)g_pSavedGameData)[0x5C]) {
                        status = 0;
                    }
                } else if ((handle = File_Open(device + 10, GETP(file.name), 0, NULL)) != -1) {
                    File_Close(handle);
                    status = 9;
                } else {
                    FileManager_GetDeviceFreeSpace(device + 10, &freeBytes, &totalBytes);
                    if (FileManager_GetDeviceStatus(device + 10) == 0) {
                        status = 2;
                    } else if (freeBytes < 0x1C000) {
                        status = 3;
                    } else {
                        status = 0;
                    }
                }
            }
            s_curStatus = status;
            return status;
        } else if (s_curMemStatus == 3) {
            switch (FileManager_GetDeviceLastError(device + 10)) {
            case -11:
                status = 14;
                break;
            case -10:
                status = 13;
                break;
            case -9:
                status = 12;
                break;
            case -8:
                status = 11;
                break;
            case -7:
                status = 10;
                break;
            case -6:
                status = 8;
                break;
            case -5:
                status = 4;
                break;
            case -4:
                status = 5;
                break;
            case -3:
                status = 6;
                break;
            case -2:
                status = 7;
                break;
            case 0:
                break;
            case -1:
            default:
                status = 3;
                break;
            }
        }
    } while (s_curMemStatus == 2);
    s_curStatus = status;
    return status;
}


void GameSettings_DetermineIfOverwritingFile(void)
{
    if (g_bValidFileName[g_codeVariables.fileIndex - 1])
    {
        g_codeVariables.bOverwritingFile = 1;
    }
    else
    {
        g_codeVariables.bOverwritingFile = 0;
    }
}


void GameSettings_GetSaveGameName(s32 *letters) {
    MCP *mcp;
    char name[12];
    s32 i;

    if (g_bValidFileName[g_codeVariables.fileIndex - 1]) {
        strncpy(name, g_fileNames[g_codeVariables.fileIndex - 1], 11);
    } else {
        mcp = GameSettings_LoadMCP();
        switch (mcp != NULL ? mcp->gameType : 0) {
        case 0:
            sprintf(name, "HW VEL X %d", g_codeVariables.fileIndex);
            break;
        case 1:
            sprintf(name, "DD RESCUE%d", g_codeVariables.fileIndex);
            break;
        default:
            sprintf(name, "SAVE GAME%d", g_codeVariables.fileIndex);
            break;
        }
    }
    for (i = 0; i < 10; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            letters[i] = name[i] - 'A';
        } else if (name[i] >= '0' && name[i] <= '9') {
            letters[i] = name[i] - '0' + 26;
        } else {
            letters[i] = 36;
        }
    }
}


extern u8 g_saveFileCheckSum[];  /* typed per function below */

void GameSettings_SetSaveGameName(u32 *letters) {
    extern u8 *g_pSavedGameData;
    #define g_saveFileCheckSum (*(s32 *)(void *)&g_saveFileCheckSum)
    extern GameInfo g_gameInfo[2];
    u32 device;
    GameInfo info;
    u8 *data;
    s32 index;
    MCP *mcp;
    s32 handle;
    s32 sum;
    s32 i;

    device = g_codeVariables.device;
    for (i = 0; i < 10; i++) {
        if (letters[i] < 26) {
            g_saveFileName[i] = (s8)letters[i] + 'A';
        } else if (letters[i] < 36) {
            g_saveFileName[i] = letters[i] - 26 + '0';
        } else {
            g_saveFileName[i] = ' ';
        }
    }
    g_saveFileName[10] = 0;
    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        SETP(info.fileName, GETP(g_gameInfo[mcp->gameType].fileName));
        index = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(info.fileName, "saveFile");
        index = 0;
    }
    data = g_pSavedGameData;
    handle = File_Open(device + 0xA, GETP(info.fileName), 0, 0);
    if (handle != -1) {
        File_Read(handle, index * 8 + 0x15C, data);
        File_Close(handle);
        sum = *(s32 *)(data + 8);
    } else {
        sum = -1;
    }
    g_saveFileCheckSum = sum;
#undef g_saveFileCheckSum
}



char *GameSettings_GetActiveMemoryCardFileName(s8 bNoDefault, s32 index, char *defaultName) {
    typedef struct SaveSlot {
        u8 valid;
        char name[11];
        s32 year;
        s32 month;
        s32 day;
        u32 hour;
        s32 minute;
    } SaveSlot;
    typedef struct SaveData {
        u8 header[0x5C];
        SaveSlot slots[8];
    } SaveData;
    extern u8 *g_pSavedGameData;
    static s32 cachedStatus = -1;
    static u32 cachedMemorySlot = (u32)-1;
    u32 i;
    u32 size;
    u32 version;
    u8 *data;
    s32 handle;
    s8 ok;
    u32 slot;  /* port: memory card slot number */
    s32 status;
    GameInfo info;
    MCP *mcp;
    SaveData *save;
    u32 hour;
    u32 hour12;

    slot = g_codeVariables.device - 1;
    status = GameSettings_GetMemoryCardStatus((s32)g_codeVariables.device, 0);
    if (cachedMemorySlot != slot || status != cachedStatus || g_bRefreshMC_FileList) {
        cachedMemorySlot = slot;
        cachedStatus = status;
        g_bRefreshMC_FileList = 0;
        memset(g_bValidFileName, 0, 8);
        if (status == 1) {
            mcp = GameSettings_LoadMCP();
            if (mcp != NULL) {
                SETP(info.fileName, GETP(g_gameInfo[mcp->gameType].fileName));
                version = g_gameInfo[mcp->gameType].unk14;
                size = g_gameInfo[mcp->gameType].unk18;
            } else {
                SETP(info.fileName, "saveFile");
                info.unk14 = 0;
                version = info.unk14;
                size = version;
            }
            data = g_pSavedGameData;
            ok = 0;
            if ((handle = File_Open(slot + 0xB, GETP(info.fileName), 0, NULL)) != -1) {
                File_Read(handle, size * 8 + 0x15C, data);
                if (((u32 *)data)[0] == 4 && ((u32 *)data)[1] == version &&
                    ((u32 *)data)[2] == FileManager_GetChecksum(version, data + 0xC)) {
                    ok = 1;
                }
                File_Close(handle);
            }
            if (ok) {
                save = (SaveData *)g_pSavedGameData;
                for (i = 0; i < 8; i++) {
                    g_bValidFileName[i] = save->slots[i].valid;
                    if (g_bValidFileName[i] == 0) {
                        strcpy(g_fileNames[i], defaultName);
                        break;
                    }
                    hour = save->slots[i].hour;
                    hour12 = hour;
                    if (hour == 0) {
                        hour12 = 12;
                    } else if (hour > 12) {
                        hour12 = hour - 12;
                    }
                    sprintf(g_fileNames[i], "%10s    %2d/%2d/%4d %2d:%02d %s", save->slots[i].name,
                            save->slots[i].month, save->slots[i].day, save->slots[i].year, hour12,
                            save->slots[i].minute, hour >= 12 ? "PM" : "AM");
                }
            }
        }
    }
    if (g_bValidFileName[index - 1]) {
        return g_fileNames[index - 1];
    }
    if (!bNoDefault && (index - 1 == 0 || g_bValidFileName[index - 2])) {
        return defaultName;
    }
    return NULL;
}



char *GameSettings_GetActiveMemoryCardLevelDetails(void) {
    extern u8 *g_pSavedGameData;
    extern s8 g_bRefreshMC_FileInfo;
    typedef struct SaveSlot {
        s8 valid;
        u8 unk1[0x1F];
    } SaveSlot;
    typedef struct SaveData {
        u8 header[0x5C];
        SaveSlot slots[8];
    } SaveData;
    static s32 cachedStatus = -1;
    static u32 cachedMemorySlot = (u32)-1;
    static u32 cachedFileSlot = -1;
    static char levelDetails[256];
    u32 slot;  /* port: memory card slot number */
    s32 status;
    u32 fileSlot;
    SaveData *data;
    MCP *mcp;
    s8 *save;
    u32 i;
    u32 mission;
    u32 challenge;
    u32 cars;
    u32 items;
    u32 battles;
    u32 races;

    slot = g_codeVariables.device - 1;
    status = GameSettings_GetMemoryCardStatus((s32)g_codeVariables.device, 0);
    fileSlot = g_codeVariables.fileIndex - 1;
    if (cachedMemorySlot != slot || status != cachedStatus) {
        g_bRefreshMC_FileInfo = 1;
    }
    if (cachedFileSlot != fileSlot || g_bRefreshMC_FileInfo) {
        cachedMemorySlot = slot;
        cachedFileSlot = fileSlot;
        cachedStatus = status;
        if (fileSlot >= 8 || status != 1) {
            return NULL;
        }
        data = (SaveData *)g_pSavedGameData;
        mcp = GameSettings_GetMCP();
        switch (mcp != NULL ? mcp->gameType : 0) {
        case 0:
            save = (s8 *)(g_pSavedGameData + cachedFileSlot * 0xF4 + 0x15C);
            if (data->slots[cachedFileSlot].valid) {
                mission = 1;
                for (i = 5; i < 19; i++) {
                    if (save[0x21 + i]) {
                        mission = i;
                    }
                }
                challenge = 1;
                for (i = 39; i < 57; i++) {
                    if (save[0x21 + i]) {
                        challenge = i;
                    }
                }
                cars = 0;
                for (i = 0; i < 33; i++) {
                    if (save[i]) {
                        cars++;
                    }
                }
                items = 0;
                for (i = 0; i < 9; i++) {
                    if (save[0x96 + i]) {
                        items++;
                    }
                }
                races = 0;
                for (i = 0; i < 2; i++) {
                    if (save[0x42 + i]) {
                        races++;
                    }
                }
                battles = 0;
                for (i = 0; i < 4; i++) {
                    if (save[0x38 + i]) {
                        battles++;
                    }
                }
                sprintf(levelDetails, "Mission %d|Challenge %d|Available Cars %d|Available Items %d|Available Races %d|Available Battles %d|Completed %d%%",
                        mission - 4, challenge - 38, cars, items + 3, races + 4, battles + 4,
                        (u32)(100.0f * ((f32)(cars + items + (mission - 4) + (challenge - 38) + (races + 4) + (battles + 4) - 13) / 75.0f)));
                g_codeVariables.unk124 = mission - 5;
            } else {
                levelDetails[0] = 0;
                g_codeVariables.unk124 = 0;
            }
            break;
        case 1:
            levelDetails[0] = 0;
            g_codeVariables.unk124 = 0;
            break;
        }
    } else {
        levelDetails[0] = 0;
        g_codeVariables.unk124 = 0;
    }
    return levelDetails;
}



u8 GameSettings_SaveGeneralGameData(void) {
    extern u8 *g_pSavedGameData;
    GameInfo info;
    s32 status;
    s32 handle;
    s8 ok;
    u8 *data;
    u32 version;
    u32 size;
    s32 device;
    MCP *mcp;

    status = GameSettings_GetDefaultMemoryCardStatus(&device, 1);
    if (status != 1 && status != 0) {
        return 0;
    }
    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        SETP(info.fileName, GETP(g_gameInfo[mcp->gameType].fileName));
        version = g_gameInfo[mcp->gameType].unk14;
        size = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(info.fileName, "saveFile");
        info.unk14 = 0;
        version = info.unk14;
        size = version;
    }
    data = g_pSavedGameData;
    ok = 0;
    if ((handle = File_Open(device + 0xA, GETP(info.fileName), 0, NULL)) != -1) {
        File_Read(handle, size * 8 + 0x15C, data);
        if (((u32 *)data)[0] == 4 && ((u32 *)data)[1] == version &&
            ((u32 *)data)[2] == FileManager_GetChecksum(version, data + 0xC)) {
            ok = 1;
        }
        File_Close(handle);
    }
    if (!ok) {
        memset(data + 0x5C, 0, 0x100);
    }
    memcpy((u8 *)&g_generalGameSettings + 0x50, data + 0x5C, 0x100);
    memcpy(data + 0xC, &g_generalGameSettings, 0x150);
    data = g_pSavedGameData;
    ok = 0;
    if ((handle = File_Open(device + 0xA, GETP(info.fileName), 5, NULL)) != -1) {
        ((u32 *)data)[0] = 4;
        ((u32 *)data)[1] = version;
        ((u32 *)data)[2] = FileManager_GetChecksum(version, data + 0xC);
        if (File_Write(handle, size * 8 + 0x15C, data)) {
            ok = 1;
        }
        File_Close(handle);
    }
    if (ok) {
        g_codeVariables.unk11C[0] = 1;
    } else {
        g_codeVariables.unk11C[0] = 0;
    }
    return g_codeVariables.unk11C[0];
}



u8 GameSettings_SaveGame(void) {
    typedef struct SaveSlot {
        u8 valid;
        char name[11];
        s32 year;
        s32 month;
        s32 day;
        u32 hour;
        s32 minute;
    } SaveSlot;
    typedef struct SaveData {
        u8 header[0x5C];
        SaveSlot slots[8];
    } SaveData;
    typedef struct SaveFile {
        u32 device;
        PTR32(const char) name;
        u32 version;
        u32 size;
    } SaveFile;
    void GetSystemDate(s32 *year, s32 *month, s32 *day, u32 *hour, s32 *minute);
    extern u8 *g_pSavedGameData;
    extern u8 *g_pGameData;
    #define g_saveFileCheckSum (*(u32 *)(void *)&g_saveFileCheckSum)
    SaveFile file;
    u8 *data;
    u32 slot;
    s32 status;
    s8 ok;
    s32 handle;
    MCP *mcp;

    file.device = g_codeVariables.device;
    slot = g_codeVariables.fileIndex - 1;
    status = GameSettings_GetMemoryCardStatus(file.device, 1);
    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        SETP(file.name, GETP(g_gameInfo[mcp->gameType].fileName));
        file.version = g_gameInfo[mcp->gameType].unk14;
        file.size = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(file.name, "saveFile");
        file.version = 0;
        file.size = file.version;
    }
    if ((status != 0 && status != 1) || g_saveFileCheckSum != GameSettings_GetSaveFileChecksum(file.device, GETP(file.name), file.size)) {
        g_codeVariables.unk11C[0] = 0;
        return 0;
    }
    data = g_pSavedGameData;
    ok = 0;
    if ((handle = File_Open(file.device + 0xA, GETP(file.name), 0, NULL)) != -1) {
        File_Read(handle, file.size * 8 + 0x15C, data);
        if (((u32 *)data)[0] == 4 && ((u32 *)data)[1] == file.version &&
            ((u32 *)data)[2] == FileManager_GetChecksum(file.version, data + 0xC)) {
            ok = 1;
        }
        File_Close(handle);
    }
    if (!ok) {
        memset(data + 0x5C, 0, 0x100);
    }
    while (slot != 0) {
        memcpy(&g_pSavedGameData[slot * file.size + 0x15C], &g_pSavedGameData[(slot - 1) * file.size + 0x15C], file.size);
        memcpy(&((SaveData *)data)->slots[slot], &((SaveData *)data)->slots[slot - 1], sizeof(SaveSlot));
        slot--;
    }
    memcpy((u8 *)&g_generalGameSettings + 0x50, data + 0x5C, 0x100);
    memcpy(data + 0xC, &g_generalGameSettings, 0x150);
    ((SaveData *)data)->slots[0].valid = 1;
    strcpy(((SaveData *)data)->slots[0].name, g_saveFileName);
    GetSystemDate(&((SaveData *)data)->slots[0].year, &((SaveData *)data)->slots[0].month,
                  &((SaveData *)data)->slots[0].day, &((SaveData *)data)->slots[0].hour,
                  &((SaveData *)data)->slots[0].minute);
    GameSettings_GatherGameData();
    memcpy(g_pSavedGameData + 0x15C, g_pGameData, file.size);
    {
        s8 wok;
        s32 whandle;
        u8 *wdata;
        wdata = g_pSavedGameData;
        wok = 0;
        if ((whandle = File_Open(file.device + 0xA, GETP(file.name), 5, NULL)) != -1) {
            ((u32 *)wdata)[0] = 4;
            ((u32 *)wdata)[1] = file.version;
            ((u32 *)wdata)[2] = FileManager_GetChecksum(file.version, wdata + 0xC);
            if (File_Write(whandle, file.size * 8 + 0x15C, wdata)) {
                wok = 1;
            }
            File_Close(whandle);
        }
        if (wok) {
            g_codeVariables.unk11C[0] = 1;
        } else {
            g_codeVariables.unk11C[0] = 0;
        }
    }
    g_bRefreshMC = 1;
    g_bRefreshMC_FileList = 1;
    g_bRefreshMC_FileInfo = 1;
    return g_codeVariables.unk11C[0];
#undef g_saveFileCheckSum
}





u8 GameSettings_LoadGame(void) {
    extern u8 *g_pSavedGameData;
    extern u8 *g_pGameData;
    extern GameInfo g_gameInfo[2];
    s32 slot;
    u32 device;
    s32 handle;
    s8 ok;
    u8 *data;
    u32 version;
    u8 *saved;
    u32 size;
    GameInfo info;
    MCP *mcp;
    u32 i;

    device = g_codeVariables.device;
    slot = g_codeVariables.fileIndex - 1;
    mcp = GameSettings_LoadMCP();
    if (mcp != NULL) {
        SETP(info.fileName, GETP(g_gameInfo[mcp->gameType].fileName));
        version = g_gameInfo[mcp->gameType].unk14;
        size = g_gameInfo[mcp->gameType].unk18;
    } else {
        SETP(info.fileName, "saveFile");
        version = 0;
        size = version;
    }
    data = g_pSavedGameData;
    ok = 0;
    if ((handle = File_Open(device + 0xA, GETP(info.fileName), 0, NULL)) != -1) {
        File_Read(handle, size * 8 + 0x15C, data);
        if (((u32 *)data)[0] == 4 && ((u32 *)data)[1] == version &&
            ((u32 *)data)[2] == FileManager_GetChecksum(version, data + 0xC)) {
            ok = 1;
        }
        File_Close(handle);
    }
    if (!ok) {
        g_codeVariables.unk11C[1] = 0;
        return 0;
    }
    saved = g_pSavedGameData;
    memcpy(g_pGameData, &saved[slot * size + 0x15C], size);
    memcpy(&g_generalGameSettings, saved + 0xC, 0x150);
    g_codeVariables.unk11C[1] = 1;
    g_bRefreshMC = 1;
    g_bRefreshMC_FileList = 1;
    g_bRefreshMC_FileInfo = 1;
    for (i = 0; i < 11; i++) {
        g_codeVariables.unk128[i] = 0;
    }
    return g_codeVariables.unk11C[1];
}


s32 GameSettings_FormatMemoryCard(void)
{
    u8 result;

    result = FileManager_FormatDevice(g_codeVariables.device + 0xA);
    g_codeVariables.unk11C[2] = result;
    g_bRefreshMC = 1;
    g_bRefreshMC_FileList = 1;
    g_bRefreshMC_FileInfo = 1;
    return result;
}



s32 GameSettings_DeleteGameFile(void) {
    extern GameInfo g_gameInfo[2];
    static s8 bFirstTime = 1;
    static MCP *pMCP;
    u32 device;
    MCP *mcp;
    void *pack;
    const char *name;
    u8 result;

    device = g_codeVariables.device + 0xA;
    if (bFirstTime) {
        bFirstTime = 0;
        pack = FileStreaming_InitializeDataPack(FileStreaming_LoadDataPack("mcp", 3, 0, 0), 3, 1);
        if (pack == NULL) {
            mcp = NULL;
            goto done;
        }
        pMCP = DataPack_GetScript(pack, 0);
    }
    mcp = pMCP;
done:
    name = (mcp != NULL) ? GETP(g_gameInfo[mcp->gameType].fileName) : "saveFile";
    result = File_Delete(device, name);
    g_bRefreshMC = 1;
    g_codeVariables.unk11C[2] = result;
    g_bRefreshMC_FileList = 1;
    g_bRefreshMC_FileInfo = 1;
    return result;
}


