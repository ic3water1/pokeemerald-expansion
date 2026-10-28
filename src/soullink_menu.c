#include "global.h"
#include "soullink.h"
#include "soullink_run.h"
#include "soullink_menu.h"
#include "bg.h"
#include "gpu_regs.h"
#include "international_string_util.h"
#include "main.h"
#include "main_menu.h"
#include "menu.h"
#include "new_game.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon_icon.h"
#include "save.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "strings.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/characters.h"
#include "constants/species.h"

// Window IDs
enum
{
    WIN_HEADER,
    WIN_BODY,
    WIN_FOOTER,
    WIN_COUNT,
};

// Screen States
enum SoulLinkScreen
{
    SOULLINK_SCREEN_MAIN,
    SOULLINK_SCREEN_HOST_SETUP,
    SOULLINK_SCREEN_JOIN_ENTRY,
    SOULLINK_SCREEN_WAITING,
    SOULLINK_SCREEN_LOBBY,
    SOULLINK_SCREEN_ERROR,
    SOULLINK_SCREEN_STARTING_RUN,
    SOULLINK_SCREEN_EXITING,
};

// Main Menu Choices
enum
{
    SOULLINK_OPTION_HOST,
    SOULLINK_OPTION_JOIN,
    SOULLINK_OPTION_RECONNECT,
    SOULLINK_OPTION_EXIT,
    SOULLINK_OPTION_COUNT,
};

#define TILE_TOP_CORNER_L 0x1A2
#define TILE_TOP_EDGE     0x1A3
#define TILE_TOP_CORNER_R 0x1A4
#define TILE_LEFT_EDGE    0x1A5
#define TILE_RIGHT_EDGE   0x1A7
#define TILE_BOT_CORNER_L 0x1A8
#define TILE_BOT_EDGE     0x1A9
#define TILE_BOT_CORNER_R 0x1AA

static const u16 sSoulLinkBg_Pal[] = {RGB(17, 18, 31)};
static const u16 sSoulLinkText_Pal[] = INCGFX_U16("graphics/interface/option_menu_text.pal", ".gbapal");

static const struct WindowTemplate sSoulLinkWinTemplates[] =
{
    [WIN_HEADER] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 1,
        .width = 26,
        .height = 2,
        .paletteNum = 1,
        .baseBlock = 0,
    },
    [WIN_BODY] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 4,
        .width = 26,
        .height = 12,
        .paletteNum = 1,
        .baseBlock = 52,
    },
    [WIN_FOOTER] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 17,
        .width = 26,
        .height = 2,
        .paletteNum = 1,
        .baseBlock = 364,
    },
    DUMMY_WIN_TEMPLATE,
};

static const struct BgTemplate sSoulLinkBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 1,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0,
    },
    {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 30,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0,
    },
};

// Fixed 16-Pokemon Roster for Face-Code System
static const u16 sFaceCodeSpecies[16] = {
    SPECIES_PIKACHU,    // 0
    SPECIES_BULBASAUR,  // 1
    SPECIES_CHARMANDER, // 2
    SPECIES_SQUIRTLE,   // 3
    SPECIES_EEVEE,      // 4
    SPECIES_JIGGLYPUFF, // 5
    SPECIES_MEOWTH,     // 6
    SPECIES_GENGAR,     // 7
    SPECIES_TREECKO,    // 8
    SPECIES_TORCHIC,    // 9
    SPECIES_MUDKIP,     // 10
    SPECIES_RALTS,      // 11
    SPECIES_LOTAD,      // 12
    SPECIES_SPHEAL,     // 13
    SPECIES_SABLEYE,    // 14
    SPECIES_ABSOL,      // 15
};

static const u8 *const sFaceCodeNames[16] = {
    COMPOUND_STRING("Pikachu"),
    COMPOUND_STRING("Bulbasaur"),
    COMPOUND_STRING("Charmander"),
    COMPOUND_STRING("Squirtle"),
    COMPOUND_STRING("Eevee"),
    COMPOUND_STRING("Jigglypuff"),
    COMPOUND_STRING("Meowth"),
    COMPOUND_STRING("Gengar"),
    COMPOUND_STRING("Treecko"),
    COMPOUND_STRING("Torchic"),
    COMPOUND_STRING("Mudkip"),
    COMPOUND_STRING("Ralts"),
    COMPOUND_STRING("Lotad"),
    COMPOUND_STRING("Spheal"),
    COMPOUND_STRING("Sableye"),
    COMPOUND_STRING("Absol"),
};

// 4x4 Grid Coordinates (Centers in screen pixels)
static const u8 sGridX[4] = {72, 104, 136, 168};
static const u8 sGridY[4] = {56, 74, 92, 110};

// State variables in EWRAM
static EWRAM_DATA u8 sScreen = 0;
static EWRAM_DATA u8 sPrevScreen = 0;
static EWRAM_DATA u8 sMenuCursor = 0;
static EWRAM_DATA u8 sHostMaxPlayers = 0;
static EWRAM_DATA u8 sFaceGridCursor = 0;
static EWRAM_DATA u8 sNumSelectedFaces = 0;
static EWRAM_DATA u8 sSelectedFaces[4] = {0};
static EWRAM_DATA u8 sEnteredCode[SOULLINK_ROOM_CODE_LEN] = {0};
static EWRAM_DATA u16 sWaitingTimer = 0;
static EWRAM_DATA u8 sWaitingOpCode = 0;
static EWRAM_DATA u8 sErrorMessage[80] = {0};
static EWRAM_DATA u8 sLobbyRefreshTimer = 0;
static EWRAM_DATA u8 sLobbyStartingCountdown = 0;

static EWRAM_DATA u8 sGridSpriteIds[16] = {0};
static EWRAM_DATA u8 sSlotSpriteIds[4] = {0};
static EWRAM_DATA u8 sLobbyFaceSpriteIds[4] = {0};
static EWRAM_DATA bool8 sSpritesInitialized = FALSE;
ALIGNED(4) EWRAM_DATA static u8 sFaceIconTiles[16][1024] = {{0}};

// Forward declarations
static void Task_SoulLinkMenu_Main(u8 taskId);
static void DrawBgWindowFrames(void);
static void RenderCurrentScreen(void);
static void SetErrorMessage(const u8 *msg);
static void DestroyJoinGridSprites(void);
static void CreateJoinGridSprites(void);
static void UpdateGridCursorAnimation(void);
static void DestroyLobbyFaceSprites(void);
static void CreateLobbyFaceSprites(void);
static void DestroyAllMenuSprites(void);
static void InitSoulLinkFaceIcons(void);
static u8 CreateFaceIconSprite(u8 faceIndex, s16 x, s16 y, u8 subpriority);

// Exported Face Code Helpers
u16 SoulLink_GetFaceCodeSpecies(u8 index)
{
    if (index < 16)
        return sFaceCodeSpecies[index];
    return SPECIES_NONE;
}

const u8 *SoulLink_GetFaceCodeName(u8 index)
{
    if (index < 16)
        return sFaceCodeNames[index];
    return COMPOUND_STRING("???");
}

s8 SoulLink_HexCharToFaceIndex(u8 c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool8 SoulLink_FormatFaceCodeString(u8 *dest, const u8 *indices, u8 count)
{
    static const u8 sHexDigits[] = "0123456789ABCDEF";
    u8 i;
    if (dest == NULL || indices == NULL || count != 4)
        return FALSE;

    for (i = 0; i < 4; i++)
    {
        if (indices[i] >= 16)
            return FALSE;
        dest[i] = sHexDigits[indices[i]];
    }
    dest[4] = 0;
    return TRUE;
}

bool8 SoulLink_ParseFaceCodeString(const u8 *code, u8 *outIndices, u8 count)
{
    u8 i;
    if (code == NULL || outIndices == NULL || count != 4)
        return FALSE;

    for (i = 0; i < 4; i++)
    {
        s8 idx = SoulLink_HexCharToFaceIndex(code[i]);
        if (idx < 0)
            return FALSE;
        outIndices[i] = (u8)idx;
    }
    return TRUE;
}

static void AsciiToPokemonString(u8 *dest, const u8 *asciiSrc, u32 maxChars)
{
    u32 i;
    for (i = 0; i < maxChars && asciiSrc[i] != 0; i++)
    {
        u8 c = asciiSrc[i];
        if (c >= '0' && c <= '9')
            dest[i] = CHAR_0 + (c - '0');
        else if (c >= 'A' && c <= 'Z')
            dest[i] = CHAR_A + (c - 'A');
        else if (c >= 'a' && c <= 'z')
            dest[i] = CHAR_a + (c - 'a');
        else if (c == ' ')
            dest[i] = CHAR_SPACE;
        else if (c == '-')
            dest[i] = CHAR_HYPHEN;
        else if (c == ':')
            dest[i] = CHAR_COLON;
        else if (c == '.')
            dest[i] = CHAR_PERIOD;
        else if (c == '/')
            dest[i] = CHAR_SLASH;
        else
            dest[i] = CHAR_QUESTION_MARK;
    }
    dest[i] = EOS;
}

static void PokemonStringToAscii(u8 *dest, const u8 *pokeSrc, u32 maxChars)
{
    u32 i;
    for (i = 0; i < maxChars && pokeSrc[i] != EOS; i++)
    {
        u8 c = pokeSrc[i];
        if (c >= CHAR_0 && c <= CHAR_9)
            dest[i] = '0' + (c - CHAR_0);
        else if (c >= CHAR_A && c <= CHAR_Z)
            dest[i] = 'A' + (c - CHAR_A);
        else if (c >= CHAR_a && c <= CHAR_z)
            dest[i] = 'a' + (c - CHAR_a);
        else if (c == CHAR_SPACE)
            dest[i] = ' ';
        else
            dest[i] = '?';
    }
    dest[i] = 0;
}

static void SeedLocalPlayerNameFromSave(void)
{
    if (gSaveBlock2Ptr->playerName[0] != EOS && gSaveBlock2Ptr->playerName[0] != 0)
    {
        PokemonStringToAscii(gSoulLink.players[0].name, gSaveBlock2Ptr->playerName, PLAYER_NAME_LEN - 1);
        gSoulLink.players[0].name[PLAYER_NAME_LEN - 1] = 0;
    }
    else
    {
        gSoulLink.players[0].name[0] = 'P';
        gSoulLink.players[0].name[1] = 'l';
        gSoulLink.players[0].name[2] = 'a';
        gSoulLink.players[0].name[3] = 'y';
        gSoulLink.players[0].name[4] = 'e';
        gSoulLink.players[0].name[5] = 'r';
        gSoulLink.players[0].name[6] = 0;
    }
}

// ----------------------------------------------------------------------------
// Sprite Management
// ----------------------------------------------------------------------------
static void InitSpriteIds(void)
{
    u8 i;
    for (i = 0; i < 16; i++)
        sGridSpriteIds[i] = MAX_SPRITES;
    for (i = 0; i < 4; i++)
        sSlotSpriteIds[i] = MAX_SPRITES;
    for (i = 0; i < 4; i++)
        sLobbyFaceSpriteIds[i] = MAX_SPRITES;
    sSpritesInitialized = TRUE;
}

static void DestroyJoinGridSprites(void)
{
    u8 i;
    if (!sSpritesInitialized)
        return;

    for (i = 0; i < 16; i++)
    {
        if (sGridSpriteIds[i] < MAX_SPRITES)
        {
            FreeAndDestroyMonIconSprite(&gSprites[sGridSpriteIds[i]]);
            sGridSpriteIds[i] = MAX_SPRITES;
        }
    }
    for (i = 0; i < 4; i++)
    {
        if (sSlotSpriteIds[i] < MAX_SPRITES)
        {
            FreeAndDestroyMonIconSprite(&gSprites[sSlotSpriteIds[i]]);
            sSlotSpriteIds[i] = MAX_SPRITES;
        }
    }
}

static void UpdateGridCursorAnimation(void)
{
    u8 i;
    for (i = 0; i < 16; i++)
    {
        if (sGridSpriteIds[i] < MAX_SPRITES)
        {
            struct Sprite *sprite = &gSprites[sGridSpriteIds[i]];
            if (i == sFaceGridCursor)
            {
                sprite->callback = SpriteCB_MonIcon;
                sprite->animPaused = FALSE;
            }
            else
            {
                sprite->callback = SpriteCallbackDummy;
                sprite->animPaused = TRUE;
                sprite->animCmdIndex = 0;
                sprite->animDelayCounter = 0;
                UpdateMonIconFrame(sprite);
            }
        }
    }
}

static void InitSoulLinkFaceIcons(void)
{
    static const u8 sFaceCutoffs[16] = {
        [0] = 22,  // Pikachu: head rows 10..22 (height 13), trim body 23..31
        [1] = 22,  // Bulbasaur: head rows 11..22 (height 12), trim body 23..31
        [2] = 21,  // Charmander: head rows 11..21 (height 11), trim body 22..31
        [3] = 21,  // Squirtle: head rows 13..21 (height 9), trim body 22..31
        [4] = 21,  // Eevee: head rows 12..21 (height 10), trim body 22..31
        [5] = 26,  // Jigglypuff: sphere face rows 10..26, trim feet 27..31
        [6] = 20,  // Meowth: head rows 9..20 (height 12), trim body 21..31
        [7] = 24,  // Gengar: ghost face rows 7..24 (height 18), trim feet 25..31
        [8] = 20,  // Treecko: head rows 9..20 (height 12), trim body 21..31
        [9] = 20,  // Torchic: head rows 10..20 (height 11), trim body 21..31
        [10] = 22, // Mudkip: head rows 11..22 (height 12), trim body 23..31
        [11] = 20, // Ralts: head rows 11..20 (height 10), trim body 21..31
        [12] = 26, // Lotad: lily pad & beak rows 13..26 (height 14), trim 27..31
        [13] = 27, // Spheal: sphere face rows 13..27 (height 15), trim 28..31
        [14] = 21, // Sableye: head rows 9..21 (height 13), trim body 22..31
        [15] = 20, // Absol: head & horn rows 7..20 (height 14), trim body 21..31
    };

    u8 i, f, y, tx, b;
    for (i = 0; i < 16; i++)
    {
        const u8 *srcTiles = GetMonIconTiles(sFaceCodeSpecies[i], 0);
        if (srcTiles == NULL)
            continue;

        memcpy(sFaceIconTiles[i], srcTiles, 1024);

        u8 cutoff = sFaceCutoffs[i];
        for (f = 0; f < 2; f++)
        {
            for (y = cutoff + 1; y < 32; y++)
            {
                u8 ty = y / 8;
                u8 r = y % 8;
                for (tx = 0; tx < 4; tx++)
                {
                    u16 offset = (f * 16 + ty * 4 + tx) * 32 + r * 4;
                    for (b = 0; b < 4; b++)
                    {
                        sFaceIconTiles[i][offset + b] = 0;
                    }
                }
            }
        }
    }
}

static u8 CreateFaceIconSprite(u8 faceIndex, s16 x, s16 y, u8 subpriority)
{
    if (faceIndex >= 16)
        return MAX_SPRITES;

    u8 spriteId = CreateMonIconNoPersonality(sFaceCodeSpecies[faceIndex], SpriteCallbackDummy, x, y, subpriority);
    if (spriteId < MAX_SPRITES)
    {
        gSprites[spriteId].oam.priority = 0; // Render in front of BG0 window layer (Finding 1)
        gSprites[spriteId].animPaused = TRUE;
        gSprites[spriteId].animBeginning = FALSE;
        gSprites[spriteId].animCmdIndex = 0; // Force immediate frame 0 copy (Finding 11)
        gSprites[spriteId].animDelayCounter = 0;
        gSprites[spriteId].images = (const struct SpriteFrameImage *)sFaceIconTiles[faceIndex];
        UpdateMonIconFrame(&gSprites[spriteId]);
    }
    return spriteId;
}

static void CreateJoinGridSprites(void)
{
    u8 i;
    if (!sSpritesInitialized)
        InitSpriteIds();

    DestroyJoinGridSprites();

    for (i = 0; i < 16; i++)
    {
        u8 col = i % 4;
        u8 row = i / 4;
        sGridSpriteIds[i] = CreateFaceIconSprite(i, sGridX[col], sGridY[row], 1);
    }

    // Refresh slot sprites for any already selected faces
    for (i = 0; i < sNumSelectedFaces && i < 4; i++)
    {
        u8 speciesIdx = sSelectedFaces[i];
        if (speciesIdx < 16)
        {
            sSlotSpriteIds[i] = CreateFaceIconSprite(speciesIdx, sGridX[i], 36, 0);
        }
    }

    UpdateGridCursorAnimation();
}

static void DestroyLobbyFaceSprites(void)
{
    u8 i;
    if (!sSpritesInitialized)
        return;

    for (i = 0; i < 4; i++)
    {
        if (sLobbyFaceSpriteIds[i] < MAX_SPRITES)
        {
            FreeAndDestroyMonIconSprite(&gSprites[sLobbyFaceSpriteIds[i]]);
            sLobbyFaceSpriteIds[i] = MAX_SPRITES;
        }
    }
}

static void CreateLobbyFaceSprites(void)
{
    u8 i;
    static const u8 sLobbyFaceX[4] = {64, 92, 120, 148};

    if (!sSpritesInitialized)
        InitSpriteIds();

    DestroyLobbyFaceSprites();

    for (i = 0; i < 4; i++)
    {
        s8 faceIdx = SoulLink_HexCharToFaceIndex(gSoulLink.lobbyResponse.roomCode[i]);
        if (faceIdx < 0 || faceIdx >= 16)
        {
            // Fallback for legacy alphanumeric code
            faceIdx = gSoulLink.lobbyResponse.roomCode[i] % 16;
        }

        sLobbyFaceSpriteIds[i] = CreateFaceIconSprite((u8)faceIdx, sLobbyFaceX[i], 16, 0);
    }
}

static void DestroyAllMenuSprites(void)
{
    DestroyJoinGridSprites();
    DestroyLobbyFaceSprites();
}

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

void CB2_InitSoulLinkMenu(void)
{
    switch (gMain.state)
    {
    default:
    case 0:
        SetVBlankCallback(NULL);
        gMain.state++;
        break;
    case 1:
        DmaClearLarge16(3, (void *)(VRAM), VRAM_SIZE, 0x1000);
        DmaClear32(3, OAM, OAM_SIZE);
        DmaClear16(3, PLTT, PLTT_SIZE);
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sSoulLinkBgTemplates, ARRAY_COUNT(sSoulLinkBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        InitWindows(sSoulLinkWinTemplates);
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_WIN0H, 0);
        SetGpuReg(REG_OFFSET_WIN0V, 0);
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG0 | WININ_WIN0_OBJ);
        SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG0 | WINOUT_WIN01_BG1 | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_DARKEN);
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        SetGpuReg(REG_OFFSET_BLDY, 4);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_WIN0_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
        ShowBg(0);
        ShowBg(1);
        gMain.state++;
        break;
    case 2:
        ResetPaletteFade();
        ScanlineEffect_Stop();
        ResetTasks();
        ResetSpriteData();
        gMain.state++;
        break;
    case 3:
        LoadBgTiles(1, GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->tiles, 0x120, 0x1A2);
        gMain.state++;
        break;
    case 4:
        LoadPalette(sSoulLinkBg_Pal, BG_PLTT_ID(0), sizeof(sSoulLinkBg_Pal));
        LoadPalette(GetWindowFrameTilesPal(gSaveBlock2Ptr->optionsWindowFrameType)->pal, BG_PLTT_ID(7), PLTT_SIZE_4BPP);
        LoadPalette(sSoulLinkText_Pal, BG_PLTT_ID(1), sizeof(sSoulLinkText_Pal));
        LoadMonIconPalettes();
        InitSoulLinkFaceIcons();
        gMain.state++;
        break;
    case 5:
        PutWindowTilemap(WIN_HEADER);
        PutWindowTilemap(WIN_BODY);
        PutWindowTilemap(WIN_FOOTER);
        DrawBgWindowFrames();
        gMain.state++;
        break;
    case 6:
        sScreen = SOULLINK_SCREEN_MAIN;
        sPrevScreen = SOULLINK_SCREEN_MAIN;
        sMenuCursor = 0;
        sHostMaxPlayers = 2;
        sFaceGridCursor = 0;
        sNumSelectedFaces = 0;
        sWaitingTimer = 0;
        sWaitingOpCode = 0;
        sLobbyRefreshTimer = 0;
        sLobbyStartingCountdown = 0;
        sEnteredCode[0] = 0;

        InitSpriteIds();

        SeedLocalPlayerNameFromSave();

        RenderCurrentScreen();
        CreateTask(Task_SoulLinkMenu_Main, 0);
        gMain.state++;
        break;
    case 7:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB);
        SetMainCallback2(MainCB2);
        return;
    }
}

static void DrawBgWindowFrames(void)
{
    // Draw Header Frame (x=1..28, y=0..3)
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  0, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  0,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  1,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2,  3, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28,  3,  1,  1,  7);

    // Draw Body Frame (x=1..28, y=3..16)
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  3, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  4,  1, 12,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  4,  1, 12,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, 16,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, 16, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, 16,  1,  1,  7);

    // Draw Footer Frame (x=1..28, y=16..19)
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1, 16,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2, 16, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28, 16,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1, 17,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28, 17,  1,  2,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, 19,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, 19, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, 19,  1,  1,  7);

    CopyBgTilemapBufferToVram(1);
}

static void SetErrorMessage(const u8 *msg)
{
    u32 i;
    for (i = 0; i < sizeof(sErrorMessage) - 1 && msg[i] != 0; i++)
    {
        sErrorMessage[i] = msg[i];
    }
    sErrorMessage[i] = 0;
}

// ----------------------------------------------------------------------------
// Rendering Routines
// ----------------------------------------------------------------------------
static void RenderHeader(const u8 *title)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, title, 4, 1, TEXT_SKIP_DRAW, NULL);

    u8 transport = SoulLink_GetTransportState();
    if (transport == SOULLINK_TRANSPORT_CONNECTED)
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR GREEN}{SHADOW LIGHT_GREEN}(ONLINE)"), 160, 2, TEXT_SKIP_DRAW, NULL);
    }
    else if (transport == SOULLINK_TRANSPORT_CONNECTING || transport == SOULLINK_TRANSPORT_BRIDGE_DETECTED)
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR BLUE}{SHADOW LIGHT_BLUE}(CONNECTING)"), 144, 2, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}(OFFLINE)"), 160, 2, TEXT_SKIP_DRAW, NULL);
    }

    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);
}

static void RenderFooter(const u8 *controls)
{
    FillWindowPixelBuffer(WIN_FOOTER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_FOOTER, FONT_SMALL, controls, 4, 2, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(WIN_FOOTER, COPYWIN_FULL);
}

static void RenderMainScreen(void)
{
    RenderHeader(COMPOUND_STRING("SOUL LINK MULTIPLAYER"));

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));

    static const u8 *const sOptions[SOULLINK_OPTION_COUNT] = {
        COMPOUND_STRING("HOST ROOM"),
        COMPOUND_STRING("JOIN ROOM"),
        COMPOUND_STRING("RECONNECT"),
        COMPOUND_STRING("EXIT"),
    };

    u8 i;
    for (i = 0; i < SOULLINK_OPTION_COUNT; i++)
    {
        u8 y = 6 + i * 16;
        if (i == sMenuCursor)
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}>"), 8, y, TEXT_SKIP_DRAW, NULL);
            AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, sOptions[i], 20, y, TEXT_SKIP_DRAW, NULL);
        }
        else
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, sOptions[i], 20, y, TEXT_SKIP_DRAW, NULL);
        }
    }

    u8 transport = SoulLink_GetTransportState();
    u8 runStatus = SoulLinkRun_GetStatus();

    if (runStatus == SOULLINK_RUN_FOREIGN)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}Foreign save detected!\nSaveBlock3 profile mismatch.\nSolo save cannot become multiplayer."), 8, 54, TEXT_SKIP_DRAW, NULL);
    }
    else if (runStatus == SOULLINK_RUN_CORRUPT)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}Corrupt save metadata!\nSaveBlock3 CRC mismatch.\nSave restore blocked."), 8, 54, TEXT_SKIP_DRAW, NULL);
    }
    else if (transport == SOULLINK_TRANSPORT_OFFLINE)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}No emulator bridge detected.\nOnline play needs mgba_soul_emerald.lua.\nTraditional offline play works normally."), 8, 54, TEXT_SKIP_DRAW, NULL);
    }
    else if (runStatus == SOULLINK_RUN_RECONCILE)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR BLUE}{SHADOW LIGHT_BLUE}Reconciliation required.\nConnect bridge to authorize."), 8, 54, TEXT_SKIP_DRAW, NULL);
    }
    else if (runStatus == SOULLINK_RUN_AUTHORIZED)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR GREEN}{SHADOW LIGHT_GREEN}Multiplayer run authorized.\nReady to synchronize."), 8, 54, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR DARK_GRAY}{SHADOW LIGHT_GRAY}Synchronize Pokémon Soul Emerald\nmultiplayer runs through mGBA."), 8, 60, TEXT_SKIP_DRAW, NULL);
    }

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);
    RenderFooter(COMPOUND_STRING("{DPAD_UPDOWN} SELECT   {A_BUTTON} OK   {B_BUTTON} EXIT"));
}

static void RenderHostSetupScreen(void)
{
    RenderHeader(COMPOUND_STRING("HOST ROOM SETUP"));

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Select run player capacity:"), 8, 8, TEXT_SKIP_DRAW, NULL);

    u8 capStr[32];
    capStr[0] = CHAR_C;
    capStr[1] = CHAR_A;
    capStr[2] = CHAR_P;
    capStr[3] = CHAR_A;
    capStr[4] = CHAR_C;
    capStr[5] = CHAR_I;
    capStr[6] = CHAR_T;
    capStr[7] = CHAR_Y;
    capStr[8] = CHAR_COLON;
    capStr[9] = CHAR_SPACE;
    capStr[10] = CHAR_LESS_THAN;
    capStr[11] = CHAR_SPACE;
    capStr[12] = CHAR_0 + sHostMaxPlayers;
    capStr[13] = CHAR_SPACE;
    capStr[14] = CHAR_P;
    capStr[15] = CHAR_L;
    capStr[16] = CHAR_A;
    capStr[17] = CHAR_Y;
    capStr[18] = CHAR_E;
    capStr[19] = CHAR_R;
    capStr[20] = CHAR_S;
    capStr[21] = CHAR_SPACE;
    capStr[22] = CHAR_GREATER_THAN;
    capStr[23] = EOS;

    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, capStr, 24, 28, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("(Supported range: 2 to 8 players)"), 8, 48, TEXT_SKIP_DRAW, NULL);
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("Press {A_BUTTON} to request room.\nPress {B_BUTTON} to cancel."), 8, 66, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);
    RenderFooter(COMPOUND_STRING("{DPAD_LEFTRIGHT} CHANGE   {A_BUTTON} CREATE   {B_BUTTON} CANCEL"));
}

static void RenderJoinEntryScreen(void)
{
    u8 titleBuf[40];
    StringCopy(titleBuf, COMPOUND_STRING("SELECT: "));
    StringAppend(titleBuf, sFaceCodeNames[sFaceGridCursor]);
    RenderHeader(titleBuf);

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));

    // Slots header
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("CODE:"), 4, 2, TEXT_SKIP_DRAW, NULL);

    u8 s;
    for (s = 0; s < 4; s++)
    {
        u8 localX = sGridX[s] - 16;
        if (s >= sNumSelectedFaces)
        {
            u8 slotText[8];
            slotText[0] = CHAR_LEFT_PAREN;
            slotText[1] = CHAR_SPACE;
            slotText[2] = CHAR_0 + (s + 1);
            slotText[3] = CHAR_SPACE;
            slotText[4] = CHAR_RIGHT_PAREN;
            slotText[5] = EOS;
            AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, slotText, localX - 10, 2, TEXT_SKIP_DRAW, NULL);
        }
    }

    // Grid cursor highlight box behind focused cell
    u8 c = sFaceGridCursor % 4;
    u8 r = sFaceGridCursor / 4;
    u8 cursorLocalX = sGridX[c] - 16;
    u8 cursorLocalY = sGridY[r] - 32;

    FillWindowPixelRect(WIN_BODY, 2, cursorLocalX - 12, cursorLocalY - 9, 24, 1);
    FillWindowPixelRect(WIN_BODY, 2, cursorLocalX - 12, cursorLocalY + 8, 24, 1);
    FillWindowPixelRect(WIN_BODY, 2, cursorLocalX - 12, cursorLocalY - 9, 1, 18);
    FillWindowPixelRect(WIN_BODY, 2, cursorLocalX + 11, cursorLocalY - 9, 1, 18);

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);

    if (sNumSelectedFaces < 4)
    {
        RenderFooter(COMPOUND_STRING("{A_BUTTON} SELECT   {B_BUTTON} UNDO / BACK"));
    }
    else
    {
        RenderFooter(COMPOUND_STRING("{START_BUTTON} / {A_BUTTON} JOIN   {B_BUTTON} UNDO"));
    }
}

static void RenderWaitingScreen(void)
{
    RenderHeader(COMPOUND_STRING("COMMUNICATING"));

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));

    if (sWaitingOpCode == SOULLINK_LOBBY_OP_CREATE)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Requesting new room..."), 8, 16, TEXT_SKIP_DRAW, NULL);
    }
    else if (sWaitingOpCode == SOULLINK_LOBBY_OP_JOIN)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Joining room..."), 8, 16, TEXT_SKIP_DRAW, NULL);
    }
    else if (sWaitingOpCode == SOULLINK_LOBBY_OP_RECONNECT)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Reconnecting to session..."), 8, 16, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Contacting coordinator..."), 8, 16, TEXT_SKIP_DRAW, NULL);
    }

    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("Awaiting response from coordinator.\nGameplay is never blocked.\n\nPress {B_BUTTON} to cancel."), 8, 38, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);
    RenderFooter(COMPOUND_STRING("{B_BUTTON} CANCEL"));
}

static void RenderLobbyScreen(void)
{
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_HEADER, FONT_NORMAL, COMPOUND_STRING("ROOM:"), 4, 1, TEXT_SKIP_DRAW, NULL);

    u8 transport = SoulLink_GetTransportState();
    if (transport == SOULLINK_TRANSPORT_CONNECTED)
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR GREEN}{SHADOW LIGHT_GREEN}(ONLINE)"), 160, 2, TEXT_SKIP_DRAW, NULL);
    }
    else if (transport == SOULLINK_TRANSPORT_CONNECTING || transport == SOULLINK_TRANSPORT_BRIDGE_DETECTED)
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR BLUE}{SHADOW LIGHT_BLUE}(CONNECTING)"), 144, 2, TEXT_SKIP_DRAW, NULL);
    }
    else
    {
        AddTextPrinterParameterized(WIN_HEADER, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}(OFFLINE)"), 160, 2, TEXT_SKIP_DRAW, NULL);
    }

    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);

    CreateLobbyFaceSprites();

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("ROSTER:"), 8, 2, TEXT_SKIP_DRAW, NULL);

    u8 myId = gSoulLink.lobbyResponse.assignedPlayerId;
    u8 hostId = gSoulLink.lobbyResponse.hostPlayerId;
    u8 occupiedMask = gSoulLink.lobbyResponse.occupiedSlotsMask;
    if (occupiedMask == 0)
    {
        occupiedMask = (1 << myId);
        if (hostId < MAX_PLAYERS)
            occupiedMask |= (1 << hostId);
    }

    u8 i;
    u8 lineIndex = 0;
    u8 occupiedCount = 0;
    bool8 allReady = TRUE;

    for (i = 0; i < MAX_PLAYERS; i++)
    {
        if ((occupiedMask & (1 << i)) == 0 && !gSoulLink.players[i].isConnected)
            continue;

        occupiedCount++;
        bool8 isReady = (gSoulLink.lobbyResponse.playerReadyMask & (1 << i)) != 0 || gSoulLink.players[i].isReady;
        if (!isReady)
            allReady = FALSE;

        u8 y = 14 + lineIndex * 10;
        lineIndex++;
        if (lineIndex > 8) break;

        u8 line[40];
        u32 lp = 0;

        // Player slot number (stable ID)
        line[lp++] = CHAR_0 + (i + 1);
        line[lp++] = CHAR_PERIOD;
        line[lp++] = CHAR_SPACE;

        // Name
        u8 nameGba[16];
        if (gSoulLink.players[i].name[0] != 0)
        {
            AsciiToPokemonString(nameGba, gSoulLink.players[i].name, PLAYER_NAME_LEN - 1);
        }
        else
        {
            nameGba[0] = CHAR_P;
            nameGba[1] = CHAR_l;
            nameGba[2] = CHAR_a;
            nameGba[3] = CHAR_y;
            nameGba[4] = CHAR_e;
            nameGba[5] = CHAR_r;
            nameGba[6] = EOS;
        }

        u32 np = 0;
        while (nameGba[np] != EOS && lp < 15)
        {
            line[lp++] = nameGba[np++];
        }
        while (lp < 14)
        {
            line[lp++] = CHAR_SPACE;
        }

        // Host badge
        if (i == hostId)
        {
            line[lp++] = CHAR_LEFT_PAREN;
            line[lp++] = CHAR_H;
            line[lp++] = CHAR_O;
            line[lp++] = CHAR_S;
            line[lp++] = CHAR_T;
            line[lp++] = CHAR_RIGHT_PAREN;
            line[lp++] = CHAR_SPACE;
        }
        else
        {
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
            line[lp++] = CHAR_SPACE;
        }

        line[lp] = EOS;
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, line, 8, y, TEXT_SKIP_DRAW, NULL);

        // Ready status
        if (isReady)
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR GREEN}{SHADOW LIGHT_GREEN}(READY)"), 136, y, TEXT_SKIP_DRAW, NULL);
        }
        else
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}(WAIT)"), 136, y, TEXT_SKIP_DRAW, NULL);
        }

        // Connection status
        if (gSoulLink.players[i].isConnected)
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR GREEN}{SHADOW LIGHT_GREEN}ON"), 180, y, TEXT_SKIP_DRAW, NULL);
        }
        else
        {
            AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}OFF"), 180, y, TEXT_SKIP_DRAW, NULL);
        }
    }

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);

    // Footer prompt
    bool8 isHost = (myId == hostId);
    if (isHost)
    {
        if (occupiedCount >= 2 && allReady)
        {
            RenderFooter(COMPOUND_STRING("{A_BUTTON} READY   {START_BUTTON} START RUN!   {B_BUTTON} LEAVE"));
        }
        else
        {
            RenderFooter(COMPOUND_STRING("{A_BUTTON} READY   (Wait for players)   {B_BUTTON} LEAVE"));
        }
    }
    else
    {
        if (allReady)
        {
            RenderFooter(COMPOUND_STRING("{A_BUTTON} READY   (Wait for host)   {B_BUTTON} LEAVE"));
        }
        else
        {
            RenderFooter(COMPOUND_STRING("{A_BUTTON} TOGGLE READY            {B_BUTTON} LEAVE"));
        }
    }
}

static void RenderErrorScreen(void)
{
    RenderHeader(COMPOUND_STRING("{COLOR RED}SOUL LINK ERROR"));

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Notice:"), 8, 8, TEXT_SKIP_DRAW, NULL);

    u8 gbaMsg[96];
    AsciiToPokemonString(gbaMsg, sErrorMessage, sizeof(sErrorMessage) - 1);
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, gbaMsg, 8, 28, TEXT_SKIP_DRAW, NULL);

    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("Press {A_BUTTON} or {B_BUTTON} to continue."), 8, 68, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);
    RenderFooter(COMPOUND_STRING("{A_BUTTON} / {B_BUTTON} OK"));
}

static void RenderCurrentScreen(void)
{
    switch (sScreen)
    {
    case SOULLINK_SCREEN_MAIN:
        RenderMainScreen();
        break;
    case SOULLINK_SCREEN_HOST_SETUP:
        RenderHostSetupScreen();
        break;
    case SOULLINK_SCREEN_JOIN_ENTRY:
        RenderJoinEntryScreen();
        break;
    case SOULLINK_SCREEN_WAITING:
        RenderWaitingScreen();
        break;
    case SOULLINK_SCREEN_LOBBY:
        RenderLobbyScreen();
        break;
    case SOULLINK_SCREEN_ERROR:
        RenderErrorScreen();
        break;
    default:
        break;
    }
}

// ----------------------------------------------------------------------------
// Action Handlers
// ----------------------------------------------------------------------------
static void ExitToSavedCallback(u8 taskId)
{
    DestroyAllMenuSprites();
    FreeMonIconPalettes();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    sScreen = SOULLINK_SCREEN_EXITING;
}

static void LaunchRunGame(u8 taskId)
{
    DestroyAllMenuSprites();
    FreeMonIconPalettes();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    sScreen = SOULLINK_SCREEN_STARTING_RUN;
}

static void Task_SoulLinkMenu_Main(u8 taskId)
{
    if (gPaletteFade.active)
        return;

    // Handle scene exit transition
    if (sScreen == SOULLINK_SCREEN_EXITING)
    {
        DestroyTask(taskId);
        FreeAllWindowBuffers();
        if (gMain.savedCallback != NULL)
            SetMainCallback2(gMain.savedCallback);
        else
            SetMainCallback2(CB2_InitMainMenu);
        return;
    }

    // Handle run start transition
    if (sScreen == SOULLINK_SCREEN_STARTING_RUN)
    {
#if SOULLINK_DEV_TEST
        if ((gSoulLink.mailboxFlags & SOULLINK_FLAG_DEV_SAFE_OVERWORLD) != 0)
        {
            return;
        }
#endif
        DestroyTask(taskId);
        FreeAllWindowBuffers();

        if (gMain.savedCallback == CB2_ReturnToFieldWithOpenMenu ||
            gMain.savedCallback == CB2_ReturnToField ||
            gMain.savedCallback == CB2_ReturnToFieldFadeFromBlack)
        {
            SetMainCallback2(CB2_ReturnToField);
        }
        else if (gSaveFileStatus == SAVE_STATUS_OK)
        {
            SetMainCallback2(CB2_ContinueSavedGame);
        }
        else
        {
            SetMainCallback2(CB2_NewGame);
        }
        return;
    }

    // Check automatic run start trigger from coordinator
    if (sScreen == SOULLINK_SCREEN_LOBBY)
    {
        if (gSoulLink.lobbyResponse.runState == SOULLINK_RUN_STATE_IN_PROGRESS)
        {
            PlaySE(SE_EXP);
            DestroyLobbyFaceSprites();
            LaunchRunGame(taskId);
            return;
        }

        sLobbyRefreshTimer++;
        if (sLobbyRefreshTimer >= 30)
        {
            sLobbyRefreshTimer = 0;
            RenderLobbyScreen();
        }
    }

    // Screen Input & State Processing
    switch (sScreen)
    {
    case SOULLINK_SCREEN_MAIN:
    {
        if (JOY_NEW(DPAD_UP))
        {
            sMenuCursor = (sMenuCursor + SOULLINK_OPTION_COUNT - 1) % SOULLINK_OPTION_COUNT;
            PlaySE(SE_SELECT);
            RenderMainScreen();
        }
        else if (JOY_NEW(DPAD_DOWN))
        {
            sMenuCursor = (sMenuCursor + 1) % SOULLINK_OPTION_COUNT;
            PlaySE(SE_SELECT);
            RenderMainScreen();
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            ExitToSavedCallback(taskId);
        }
        else if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);

            u8 runStatus = SoulLinkRun_GetStatus();
            if (sMenuCursor != SOULLINK_OPTION_EXIT)
            {
                if (runStatus == SOULLINK_RUN_FOREIGN)
                {
                    SetErrorMessage((const u8 *)"FOREIGN SAVE DETECTED!\nSaveBlock3 profile mismatch.\nSolo save cannot become multiplayer.");
                    sPrevScreen = SOULLINK_SCREEN_MAIN;
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                    break;
                }
                if (runStatus == SOULLINK_RUN_CORRUPT)
                {
                    SetErrorMessage((const u8 *)"CORRUPT SAVE METADATA!\nSaveBlock3 CRC mismatch.\nSave restore blocked.");
                    sPrevScreen = SOULLINK_SCREEN_MAIN;
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                    break;
                }
#if !SOULLINK_DEV_TEST
                if (runStatus == SOULLINK_RUN_SOLO)
                {
                    SetErrorMessage((const u8 *)"MULTIPLAYER RUN REQUIRED\nSolo saves cannot become multiplayer.\nUse profile launcher to create a\nnew isolated multiplayer run profile.");
                    sPrevScreen = SOULLINK_SCREEN_MAIN;
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                    break;
                }
#endif
            }

            switch (sMenuCursor)
            {
            case SOULLINK_OPTION_HOST:
                sPrevScreen = SOULLINK_SCREEN_MAIN;
                sScreen = SOULLINK_SCREEN_HOST_SETUP;
                RenderCurrentScreen();
                break;

            case SOULLINK_OPTION_JOIN:
                sPrevScreen = SOULLINK_SCREEN_MAIN;
                sScreen = SOULLINK_SCREEN_JOIN_ENTRY;
                sFaceGridCursor = 0;
                sNumSelectedFaces = 0;
                sEnteredCode[0] = 0;
                CreateJoinGridSprites();
                RenderCurrentScreen();
                break;

            case SOULLINK_OPTION_RECONNECT:
                if (gSoulLink.lobbyResponse.roomCode[0] != 0 && gSoulLink.lobbyResponse.sessionToken[0] != 0)
                {
                    sPrevScreen = SOULLINK_SCREEN_MAIN;
                    sScreen = SOULLINK_SCREEN_WAITING;
                    sWaitingOpCode = SOULLINK_LOBBY_OP_RECONNECT;
                    sWaitingTimer = 0;
                    SoulLink_LobbyRequestReconnect(gSoulLink.lobbyResponse.roomCode, gSoulLink.lobbyResponse.sessionToken);
                    RenderCurrentScreen();
                }
                else
                {
                    SetErrorMessage((const u8 *)"No prior session token stored.\nPlease Host or Join first.");
                    sPrevScreen = SOULLINK_SCREEN_MAIN;
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                }
                break;

            case SOULLINK_OPTION_EXIT:
            default:
                ExitToSavedCallback(taskId);
                break;
            }
        }
        break;
    }

    case SOULLINK_SCREEN_HOST_SETUP:
    {
        if (JOY_NEW(DPAD_LEFT))
        {
            if (sHostMaxPlayers > 2)
            {
                sHostMaxPlayers--;
                PlaySE(SE_SELECT);
                RenderHostSetupScreen();
            }
        }
        else if (JOY_NEW(DPAD_RIGHT))
        {
            if (sHostMaxPlayers < MAX_PLAYERS)
            {
                sHostMaxPlayers++;
                PlaySE(SE_SELECT);
                RenderHostSetupScreen();
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            sScreen = SOULLINK_SCREEN_MAIN;
            RenderCurrentScreen();
        }
        else if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            sPrevScreen = SOULLINK_SCREEN_HOST_SETUP;
            sWaitingOpCode = SOULLINK_LOBBY_OP_CREATE;
            sWaitingTimer = 0;

            if (SoulLink_GetTransportState() == SOULLINK_TRANSPORT_OFFLINE)
            {
                SetErrorMessage((const u8 *)"No emulator bridge detected.\nLoad mgba_soul_emerald.lua.");
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
            }
            else
            {
                SoulLink_LobbyRequestCreate(sHostMaxPlayers);
                sScreen = SOULLINK_SCREEN_WAITING;
                RenderCurrentScreen();
            }
        }
        break;
    }

    case SOULLINK_SCREEN_JOIN_ENTRY:
    {
        u8 prevCursor = sFaceGridCursor;
        u8 r = sFaceGridCursor / 4;
        u8 c = sFaceGridCursor % 4;

        if (JOY_NEW(DPAD_UP))
        {
            r = (r + 3) % 4;
            sFaceGridCursor = r * 4 + c;
        }
        else if (JOY_NEW(DPAD_DOWN))
        {
            r = (r + 1) % 4;
            sFaceGridCursor = r * 4 + c;
        }
        else if (JOY_NEW(DPAD_LEFT))
        {
            c = (c + 3) % 4;
            sFaceGridCursor = r * 4 + c;
        }
        else if (JOY_NEW(DPAD_RIGHT))
        {
            c = (c + 1) % 4;
            sFaceGridCursor = r * 4 + c;
        }

        if (sFaceGridCursor != prevCursor)
        {
            PlaySE(SE_SELECT);
            UpdateGridCursorAnimation();
            RenderJoinEntryScreen();
            break;
        }

        if (JOY_NEW(A_BUTTON))
        {
            if (sNumSelectedFaces < 4)
            {
                PlaySE(SE_SELECT);
                sSelectedFaces[sNumSelectedFaces] = sFaceGridCursor;

                // Spawn slot sprite
                u8 slot = sNumSelectedFaces;
                if (sSlotSpriteIds[slot] < MAX_SPRITES)
                {
                    FreeAndDestroyMonIconSprite(&gSprites[sSlotSpriteIds[slot]]);
                    sSlotSpriteIds[slot] = MAX_SPRITES;
                }
                sSlotSpriteIds[slot] = CreateFaceIconSprite(sFaceGridCursor, sGridX[slot], 36, 0);

                sNumSelectedFaces++;
                SoulLink_FormatFaceCodeString(sEnteredCode, sSelectedFaces, 4);
                sEnteredCode[sNumSelectedFaces] = 0;
                RenderJoinEntryScreen();
            }
            else
            {
                // All 4 selected: A confirms and submits join request
                PlaySE(SE_SELECT);
                SoulLink_FormatFaceCodeString(sEnteredCode, sSelectedFaces, 4);
                DestroyJoinGridSprites();
                sPrevScreen = SOULLINK_SCREEN_JOIN_ENTRY;
                sWaitingOpCode = SOULLINK_LOBBY_OP_JOIN;
                sWaitingTimer = 0;

                if (SoulLink_GetTransportState() == SOULLINK_TRANSPORT_OFFLINE)
                {
                    SetErrorMessage((const u8 *)"No emulator bridge detected.\nLoad mgba_soul_emerald.lua.");
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                }
                else
                {
                    SoulLink_LobbyRequestJoin(sEnteredCode);
                    sScreen = SOULLINK_SCREEN_WAITING;
                    RenderCurrentScreen();
                }
            }
        }
        else if (JOY_NEW(START_BUTTON))
        {
            if (sNumSelectedFaces == 4)
            {
                PlaySE(SE_SELECT);
                SoulLink_FormatFaceCodeString(sEnteredCode, sSelectedFaces, 4);
                DestroyJoinGridSprites();
                sPrevScreen = SOULLINK_SCREEN_JOIN_ENTRY;
                sWaitingOpCode = SOULLINK_LOBBY_OP_JOIN;
                sWaitingTimer = 0;

                if (SoulLink_GetTransportState() == SOULLINK_TRANSPORT_OFFLINE)
                {
                    SetErrorMessage((const u8 *)"No emulator bridge detected.\nLoad mgba_soul_emerald.lua.");
                    sScreen = SOULLINK_SCREEN_ERROR;
                    RenderCurrentScreen();
                }
                else
                {
                    SoulLink_LobbyRequestJoin(sEnteredCode);
                    sScreen = SOULLINK_SCREEN_WAITING;
                    RenderCurrentScreen();
                }
            }
            else
            {
                PlaySE(SE_FAILURE);
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            if (sNumSelectedFaces > 0)
            {
                sNumSelectedFaces--;
                if (sSlotSpriteIds[sNumSelectedFaces] < MAX_SPRITES)
                {
                    FreeAndDestroyMonIconSprite(&gSprites[sSlotSpriteIds[sNumSelectedFaces]]);
                    sSlotSpriteIds[sNumSelectedFaces] = MAX_SPRITES;
                }
                sEnteredCode[sNumSelectedFaces] = 0;
                RenderJoinEntryScreen();
            }
            else
            {
                DestroyJoinGridSprites();
                sScreen = SOULLINK_SCREEN_MAIN;
                RenderCurrentScreen();
            }
        }
        break;
    }

    case SOULLINK_SCREEN_WAITING:
    {
        if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            SoulLink_CancelLobbyRequest();
            sScreen = sPrevScreen;
            if (sScreen == SOULLINK_SCREEN_JOIN_ENTRY)
            {
                CreateJoinGridSprites();
            }
            RenderCurrentScreen();
            return;
        }

        sWaitingTimer++;

        // Only process responses that match our active in-flight request ID and opcode
        if (gSoulLink.lobbyResponse.ackRequestId == gSoulLink.lobbyRequest.requestId &&
            gSoulLink.lobbyResponse.opCode == sWaitingOpCode)
        {
            u8 respCode = gSoulLink.lobbyResponse.responseCode;
            if (respCode == SOULLINK_RESP_OK)
            {
                PlaySE(SE_EXP);
                sScreen = SOULLINK_SCREEN_LOBBY;
                RenderCurrentScreen();
                return;
            }
            else if (respCode == SOULLINK_RESP_ERR_NO_BRIDGE)
            {
                SetErrorMessage((const u8 *)"No emulator bridge detected.\nPlease load Lua script in mGBA.");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
            else if (respCode == SOULLINK_RESP_ERR_ROOM_NOT_FOUND)
            {
                SetErrorMessage((const u8 *)"Room not found!\nVerify the 4-Pokemon code.");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
            else if (respCode == SOULLINK_RESP_ERR_ROOM_FULL)
            {
                SetErrorMessage((const u8 *)"Room is full!\nMaximum capacity reached.");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
            else if (respCode == SOULLINK_RESP_ERR_ALREADY_STARTED)
            {
                SetErrorMessage((const u8 *)"Run has already started!");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
            else if (respCode == SOULLINK_RESP_ERR_NETWORK)
            {
                SetErrorMessage((const u8 *)"Network error reaching server.");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
            else if (respCode != SOULLINK_RESP_PENDING && respCode != SOULLINK_RESP_IDLE)
            {
                SetErrorMessage((const u8 *)"Coordinator request failed.");
                SoulLink_CancelLobbyRequest();
                sScreen = SOULLINK_SCREEN_ERROR;
                RenderCurrentScreen();
                return;
            }
        }

        if (sWaitingTimer > 600) // 10 seconds timeout
        {
            SetErrorMessage((const u8 *)"Request timed out.\nCheck network & bridge connection.");
            SoulLink_CancelLobbyRequest();
            sScreen = SOULLINK_SCREEN_ERROR;
            RenderCurrentScreen();
            return;
        }
        break;
    }

    case SOULLINK_SCREEN_LOBBY:
    {
        u8 myId = gSoulLink.lobbyResponse.assignedPlayerId;

        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            bool8 isReady = (gSoulLink.lobbyResponse.playerReadyMask & (1 << myId)) != 0 || gSoulLink.players[myId].isReady;
            SoulLink_LobbyRequestReady(!isReady);
            RenderLobbyScreen();
        }
        else if (JOY_NEW(START_BUTTON))
        {
            u8 hostId = gSoulLink.lobbyResponse.hostPlayerId;
            if (myId == hostId) // Authoritative host check (supports host migration!)
            {
                u8 occupiedMask = gSoulLink.lobbyResponse.occupiedSlotsMask;
                if (occupiedMask == 0)
                {
                    occupiedMask = (1 << myId);
                }
                u8 count = 0;
                bool8 allReady = TRUE;
                u8 i;
                for (i = 0; i < MAX_PLAYERS; i++)
                {
                    if ((occupiedMask & (1 << i)) != 0 || gSoulLink.players[i].isConnected)
                    {
                        count++;
                        bool8 r = (gSoulLink.lobbyResponse.playerReadyMask & (1 << i)) != 0 || gSoulLink.players[i].isReady;
                        if (!r) { allReady = FALSE; }
                    }
                }

                if (count >= 2 && allReady)
                {
                    PlaySE(SE_EXP);
                    SoulLink_LobbyRequestStartRun();
                }
                else
                {
                    PlaySE(SE_FAILURE);
                }
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            DestroyLobbyFaceSprites();
            SoulLink_LobbyRequestLeave();
            sScreen = SOULLINK_SCREEN_MAIN;
            RenderCurrentScreen();
        }
        break;
    }

    case SOULLINK_SCREEN_ERROR:
    {
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            SoulLink_CancelLobbyRequest();
            sScreen = sPrevScreen;
            if (sScreen == SOULLINK_SCREEN_JOIN_ENTRY)
            {
                CreateJoinGridSprites();
            }
            RenderCurrentScreen();
        }
        break;
    }

    default:
        break;
    }
}
