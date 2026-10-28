#include "global.h"
#include "soullink.h"
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
        .height = 11,
        .paletteNum = 1,
        .baseBlock = 52,
    },
    [WIN_FOOTER] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 16,
        .width = 26,
        .height = 3,
        .paletteNum = 1,
        .baseBlock = 338,
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

static const u8 sCodeCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// State variables in EWRAM
static EWRAM_DATA u8 sScreen = 0;
static EWRAM_DATA u8 sPrevScreen = 0;
static EWRAM_DATA u8 sMenuCursor = 0;
static EWRAM_DATA u8 sHostMaxPlayers = 0;
static EWRAM_DATA u8 sCodeCursor = 0;
static EWRAM_DATA u8 sEnteredCode[SOULLINK_ROOM_CODE_LEN] = {0};
static EWRAM_DATA u16 sWaitingTimer = 0;
static EWRAM_DATA u8 sWaitingOpCode = 0;
static EWRAM_DATA u8 sErrorMessage[80] = {0};
static EWRAM_DATA u8 sLobbyRefreshTimer = 0;
static EWRAM_DATA u8 sLobbyStartingCountdown = 0;

// Forward declarations
static void Task_SoulLinkMenu_Main(u8 taskId);
static void DrawBgWindowFrames(void);
static void RenderCurrentScreen(void);
static void SetErrorMessage(const u8 *msg);

// String conversion helpers
static u8 CharToIndex(u8 c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= '0' && c <= '9')
        return 26 + (c - '0');
    return 0;
}

static u8 IndexToChar(u8 idx)
{
    if (idx < 36)
        return sCodeCharset[idx];
    return 'A';
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
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG0);
        SetGpuReg(REG_OFFSET_WINOUT, WINOUT_WIN01_BG0 | WINOUT_WIN01_BG1 | WINOUT_WIN01_CLR);
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
        sCodeCursor = 0;
        sWaitingTimer = 0;
        sWaitingOpCode = 0;
        sLobbyRefreshTimer = 0;
        sLobbyStartingCountdown = 0;

        SeedLocalPlayerNameFromSave();

        // Default code: "AAAAAA"
        sEnteredCode[0] = 'A';
        sEnteredCode[1] = 'A';
        sEnteredCode[2] = 'A';
        sEnteredCode[3] = 'A';
        sEnteredCode[4] = 'A';
        sEnteredCode[5] = 'A';
        sEnteredCode[6] = 0;

        // If a roomCode was already in the mailbox request, initialize code entry with it
        if (gSoulLink.lobbyRequest.roomCode[0] != 0)
        {
            u32 i;
            for (i = 0; i < 6 && gSoulLink.lobbyRequest.roomCode[i] != 0; i++)
            {
                sEnteredCode[i] = gSoulLink.lobbyRequest.roomCode[i];
            }
            sEnteredCode[6] = 0;
        }

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

    // Draw Body Frame (x=1..28, y=3..15)
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2,  3, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28,  3,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1,  4,  1, 11,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28,  4,  1, 11,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_L,  1, 15,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_EDGE,      2, 15, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_BOT_CORNER_R, 28, 15,  1,  1,  7);

    // Draw Footer Frame (x=1..28, y=15..19)
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_L,  1, 15,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_EDGE,      2, 15, 26,  1,  7);
    FillBgTilemapBufferRect(1, TILE_TOP_CORNER_R, 28, 15,  1,  1,  7);
    FillBgTilemapBufferRect(1, TILE_LEFT_EDGE,     1, 16,  1,  3,  7);
    FillBgTilemapBufferRect(1, TILE_RIGHT_EDGE,   28, 16,  1,  3,  7);
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
    if (transport == SOULLINK_TRANSPORT_OFFLINE)
    {
        AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{COLOR RED}{SHADOW LIGHT_RED}No emulator bridge detected.\nOnline play needs mgba_soul_emerald.lua.\nTraditional offline play works normally."), 8, 54, TEXT_SKIP_DRAW, NULL);
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
    RenderHeader(COMPOUND_STRING("JOIN ROOM SETUP"));

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, COMPOUND_STRING("Enter 6-character room code:"), 8, 6, TEXT_SKIP_DRAW, NULL);

    u8 displayCode[32];
    u32 i, pos = 0;
    displayCode[pos++] = CHAR_LEFT_PAREN;
    displayCode[pos++] = CHAR_SPACE;

    for (i = 0; i < 6; i++)
    {
        u8 c = sEnteredCode[i];
        if (c >= '0' && c <= '9')
            displayCode[pos++] = CHAR_0 + (c - '0');
        else if (c >= 'A' && c <= 'Z')
            displayCode[pos++] = CHAR_A + (c - 'A');
        else
            displayCode[pos++] = CHAR_QUESTION_MARK;

        displayCode[pos++] = CHAR_SPACE;
        displayCode[pos++] = CHAR_SPACE;
    }
    displayCode[pos++] = CHAR_RIGHT_PAREN;
    displayCode[pos++] = EOS;

    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, displayCode, 36, 26, TEXT_SKIP_DRAW, NULL);

    // Draw cursor arrow under active slot
    u8 cursorStr[32];
    u32 cpos = 0;
    cursorStr[cpos++] = CHAR_SPACE;
    cursorStr[cpos++] = CHAR_SPACE;
    for (i = 0; i < sCodeCursor; i++)
    {
        cursorStr[cpos++] = CHAR_SPACE;
        cursorStr[cpos++] = CHAR_SPACE;
        cursorStr[cpos++] = CHAR_SPACE;
    }
    cursorStr[cpos++] = CHAR_UP_ARROW;
    cursorStr[cpos++] = EOS;

    AddTextPrinterParameterized(WIN_BODY, FONT_NORMAL, cursorStr, 36, 38, TEXT_SKIP_DRAW, NULL);

    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("{DPAD_UPDOWN} Change char (A-Z, 0-9)\n{DPAD_LEFTRIGHT} Select slot (1-6)\n{START_BUTTON} or {A_BUTTON} on last slot to join"), 8, 52, TEXT_SKIP_DRAW, NULL);

    CopyWindowToVram(WIN_BODY, COPYWIN_FULL);
    RenderFooter(COMPOUND_STRING("{DPAD_UPDOWN} EDIT CODE   {START_BUTTON} JOIN   {B_BUTTON} BACK"));
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
    u8 titleBuf[48];
    u8 codeGba[16];
    AsciiToPokemonString(codeGba, gSoulLink.lobbyResponse.roomCode, 6);

    u32 pos = 0;
    titleBuf[pos++] = CHAR_R;
    titleBuf[pos++] = CHAR_O;
    titleBuf[pos++] = CHAR_O;
    titleBuf[pos++] = CHAR_M;
    titleBuf[pos++] = CHAR_COLON;
    titleBuf[pos++] = CHAR_SPACE;

    u32 ci = 0;
    while (codeGba[ci] != EOS && pos < 20)
    {
        titleBuf[pos++] = codeGba[ci++];
    }
    titleBuf[pos++] = CHAR_SPACE;
    titleBuf[pos++] = CHAR_SPACE;
    titleBuf[pos++] = CHAR_P;
    titleBuf[pos++] = CHAR_L;
    titleBuf[pos++] = CHAR_A;
    titleBuf[pos++] = CHAR_Y;
    titleBuf[pos++] = CHAR_E;
    titleBuf[pos++] = CHAR_R;
    titleBuf[pos++] = CHAR_S;
    titleBuf[pos++] = CHAR_COLON;
    titleBuf[pos++] = CHAR_SPACE;
    titleBuf[pos++] = CHAR_0 + gSoulLink.playerCount;
    titleBuf[pos++] = EOS;

    RenderHeader(titleBuf);

    FillWindowPixelBuffer(WIN_BODY, PIXEL_FILL(1));
    AddTextPrinterParameterized(WIN_BODY, FONT_SMALL, COMPOUND_STRING("ROSTER:"), 8, 2, TEXT_SKIP_DRAW, NULL);

    u8 myId = gSoulLink.lobbyResponse.assignedPlayerId;
    u8 count = gSoulLink.playerCount;
    if (count < 1) count = 1;
    if (count > MAX_PLAYERS) count = MAX_PLAYERS;

    u8 i;
    for (i = 0; i < count; i++)
    {
        u8 y = 14 + i * 10;
        u8 line[40];
        u32 lp = 0;

        // Player slot number
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
        if (i == 0)
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
        bool8 isReady = (gSoulLink.lobbyResponse.playerReadyMask & (1 << i)) != 0 || gSoulLink.players[i].isReady;
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
    bool8 allReady = TRUE;
    for (i = 0; i < count; i++)
    {
        bool8 r = (gSoulLink.lobbyResponse.playerReadyMask & (1 << i)) != 0 || gSoulLink.players[i].isReady;
        if (!r) { allReady = FALSE; break; }
    }

    if (myId == 0)
    {
        if (count >= 2 && allReady)
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
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    sScreen = SOULLINK_SCREEN_EXITING;
}

static void LaunchRunGame(u8 taskId)
{
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
        if (JOY_NEW(DPAD_UP))
        {
            u8 idx = CharToIndex(sEnteredCode[sCodeCursor]);
            idx = (idx + 1) % 36;
            sEnteredCode[sCodeCursor] = IndexToChar(idx);
            PlaySE(SE_SELECT);
            RenderJoinEntryScreen();
        }
        else if (JOY_NEW(DPAD_DOWN))
        {
            u8 idx = CharToIndex(sEnteredCode[sCodeCursor]);
            idx = (idx + 35) % 36;
            sEnteredCode[sCodeCursor] = IndexToChar(idx);
            PlaySE(SE_SELECT);
            RenderJoinEntryScreen();
        }
        else if (JOY_NEW(DPAD_LEFT))
        {
            if (sCodeCursor > 0)
            {
                sCodeCursor--;
                PlaySE(SE_SELECT);
                RenderJoinEntryScreen();
            }
        }
        else if (JOY_NEW(DPAD_RIGHT))
        {
            if (sCodeCursor < 5)
            {
                sCodeCursor++;
                PlaySE(SE_SELECT);
                RenderJoinEntryScreen();
            }
        }
        else if (JOY_NEW(B_BUTTON))
        {
            PlaySE(SE_SELECT);
            if (sCodeCursor > 0)
            {
                sCodeCursor--;
                RenderJoinEntryScreen();
            }
            else
            {
                sScreen = SOULLINK_SCREEN_MAIN;
                RenderCurrentScreen();
            }
        }
        else if (JOY_NEW(START_BUTTON) || (JOY_NEW(A_BUTTON) && sCodeCursor == 5))
        {
            PlaySE(SE_SELECT);
            sEnteredCode[6] = 0;
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
        else if (JOY_NEW(A_BUTTON))
        {
            if (sCodeCursor < 5)
            {
                sCodeCursor++;
                PlaySE(SE_SELECT);
                RenderJoinEntryScreen();
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
                SetErrorMessage((const u8 *)"Room not found!\nVerify the 6-character code.");
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
            if (myId == 0) // Host only
            {
                u8 count = gSoulLink.playerCount;
                bool8 allReady = TRUE;
                u8 i;
                for (i = 0; i < count; i++)
                {
                    bool8 r = (gSoulLink.lobbyResponse.playerReadyMask & (1 << i)) != 0 || gSoulLink.players[i].isReady;
                    if (!r) { allReady = FALSE; break; }
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
            RenderCurrentScreen();
        }
        break;
    }

    default:
        break;
    }
}
