#include "global.h"
#include "soullink_feedback.h"
#include "soullink.h"
#include "soullink_run.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "palette.h"
#include "event_data.h"
#include "menu.h"
#include "window.h"
#include "text_window.h"
#include "text.h"
#include "string_util.h"
#include "region_map.h"
#include "pokemon.h"
#include "field_message_box.h"
#include "map_name_popup.h"
#include "strings.h"
#include "constants/flags.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"

#define NOTICE_CAPACITY 8
struct Notice { u16 species, progress; u8 kind, player, route; };
static EWRAM_DATA struct Notice sQueue[NOTICE_CAPACITY] = {0};
static EWRAM_DATA u8 sFeedbackCount = 0;
static EWRAM_DATA u8 sFeedbackWindow = 0;
static EWRAM_DATA bool8 sFeedbackVisible = FALSE;
static EWRAM_DATA u8 sFeedbackActiveKind = 0;
static EWRAM_DATA u16 sFeedbackTimer = 0;
static EWRAM_DATA u16 sFeedbackDropped = 0;
static EWRAM_DATA u16 sLastBanner = 0;
static EWRAM_DATA bool8 sLastBannerValid = FALSE;
static EWRAM_DATA u8 sTransport = 0;
static EWRAM_DATA u8 sRunStatus = 0;
static EWRAM_DATA bool8 sNeedsSynchronized = FALSE;
static const u8 sAvailable[] = _("Catch Available");
static const u8 sUsed[] = _("Catch Used");
static const u8 sForfeited[] = _("Catch Forfeited");
static const u8 sLocked[] = _("Catch Locked");
static const u8 sSyncing[] = _("Syncing...");

// Source-verified ordinary Hoenn encounters only. This is a presentation allowlist,
// NOT a new encounter registry or a change to saved encounter identities.
// Route 104 and duplicated MAPSEC dungeons remain unvalidated and omitted.
static bool8 HasVerifiedOpportunity(u16 section)
{
    if (section >= MAPSEC_ROUTE_101 && section <= MAPSEC_ROUTE_134 && section != MAPSEC_ROUTE_104)
        return TRUE;
    switch (section)
    {
    case MAPSEC_PETALBURG_CITY: case MAPSEC_DEWFORD_TOWN: case MAPSEC_SLATEPORT_CITY:
    case MAPSEC_LILYCOVE_CITY: case MAPSEC_MOSSDEEP_CITY: case MAPSEC_SOOTOPOLIS_CITY:
    case MAPSEC_PACIFIDLOG_TOWN: case MAPSEC_EVER_GRANDE_CITY:
    case MAPSEC_PETALBURG_WOODS: case MAPSEC_GRANITE_CAVE: case MAPSEC_RUSTURF_TUNNEL:
    case MAPSEC_SHOAL_CAVE: case MAPSEC_VICTORY_ROAD: case MAPSEC_SAFARI_ZONE:
        return TRUE;
    default: return FALSE;
    }
}
u8 SoulLinkFeedback_GetCatchStatus(void)
{
    u16 id = SoulLink_GetEncounterId();
    u8 status = SoulLinkRun_GetStatus();
    if (gMapHeader.mapType == MAP_TYPE_INDOOR || gMapHeader.mapType == MAP_TYPE_SECRET_BASE || id >= NUM_MAPSECS || !HasVerifiedOpportunity(gMapHeader.regionMapSectionId)) return SL_CATCH_NONE;
    if (!FlagGet(FLAG_ADVENTURE_STARTED) && gSoulLink.routeState[id] == ROUTE_STATE_OPEN) return SL_CATCH_LOCKED;
    if (status != SOULLINK_RUN_SOLO && (status != SOULLINK_RUN_AUTHORIZED
        || gSoulLink.transportState != SOULLINK_TRANSPORT_CONNECTED
        || gSoulLink.eventHead != gSoulLink.eventTail || !SoulLinkRun_CanProgress())) return SL_CATCH_SYNCING;
    switch (gSoulLink.routeState[id])
    {
    case ROUTE_STATE_OPEN: return SL_CATCH_AVAILABLE;
    case ROUTE_STATE_ALIVE: case ROUTE_STATE_BOXED: return SL_CATCH_USED;
    case ROUTE_STATE_DEAD_FORFEITED: return SL_CATCH_FORFEITED;
    default: return SL_CATCH_SYNCING;
    }
}
const u8 *SoulLinkFeedback_StatusText(u8 status)
{
    switch (status) {
    case SL_CATCH_AVAILABLE: return sAvailable; case SL_CATCH_USED: return sUsed;
    case SL_CATCH_FORFEITED: return sForfeited; case SL_CATCH_LOCKED: return sLocked;
    case SL_CATCH_SYNCING: return sSyncing; default: return COMPOUND_STRING("");
    }
}
void SoulLinkFeedback_EnterArea(u16 id)
{
    if (id != sLastBanner) { sLastBanner=id; sLastBannerValid=FALSE; }
}
bool8 SoulLinkFeedback_BannerEntry(void)
{
    u16 id = gMapHeader.regionMapSectionId;
    SoulLinkFeedback_EnterArea(id);
    return !(sLastBannerValid && HasVerifiedOpportunity(id));
}
void SoulLinkFeedback_BannerShown(void) { sLastBannerValid=TRUE; }
void SoulLinkFeedback_Hide(void)
{
    if (sFeedbackVisible) { ClearStdWindowAndFrame(sFeedbackWindow, TRUE); RemoveWindow(sFeedbackWindow); sFeedbackVisible = FALSE; }
    sFeedbackTimer = 0; sFeedbackActiveKind = 0;
}
void SoulLinkFeedback_WindowsReset(void) { sFeedbackVisible = FALSE; sFeedbackTimer = 0; sFeedbackActiveKind = 0; }
void SoulLinkFeedback_Init(void)
{
    SoulLinkFeedback_Hide(); sFeedbackCount = 0; sFeedbackDropped = 0; sLastBannerValid = FALSE;
    sTransport = gSoulLink.transportState; sRunStatus = SoulLinkRun_GetStatus(); sNeedsSynchronized = FALSE;
}
u8 SoulLinkFeedback_Pending(void) { return sFeedbackCount; }
u16 SoulLinkFeedback_Dropped(void) { return sFeedbackDropped; }
static u8 Priority(u8 kind) { return kind == SL_NOTICE_DEATH || kind == SL_NOTICE_MISS || kind == SL_NOTICE_LOST ? 2 : 1; }
static void Remove(u8 at) { u32 i; for (i = at; i + 1 < sFeedbackCount; i++) sQueue[i] = sQueue[i + 1]; sFeedbackCount--; }
void SoulLinkFeedback_Enqueue(u8 kind, u8 player, u8 route, u16 species, u16 progress)
{
    u32 i, victim = 0;
    if (kind < SL_NOTICE_CATCH || kind > SL_NOTICE_SYNCED || player >= MAX_PLAYERS || species >= NUM_SPECIES) return;
    if (kind >= SL_NOTICE_LOST) {
        if (sFeedbackVisible && kind == sFeedbackActiveKind) return;
        for (i=0;i<sFeedbackCount;i++) if (sQueue[i].kind==kind) return;
    }
    if (sFeedbackCount == NOTICE_CAPACITY) {
        for (i = 1; i < sFeedbackCount; i++) if (Priority(sQueue[i].kind) < Priority(sQueue[victim].kind)) victim = i;
        if (sFeedbackDropped != 0xFFFF) sFeedbackDropped++;
        if (Priority(kind) < Priority(sQueue[victim].kind)) return;
        Remove(victim);
    }
    sQueue[sFeedbackCount++] = (struct Notice){.species=species,.progress=progress,.kind=kind,.player=player,.route=route};
}
bool8 SoulLinkFeedback_CanPresent(void)
{
    // Deliberately stricter than the integrity pump's starter/developer safe points.
    u32 i;
    if (gMain.callback2 != CB2_Overworld || gMain.inBattle || gPaletteFade.active
        || ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked() || !IsFieldMessageBoxHidden()) return FALSE;
    for (i=1;i<WINDOWS_MAX;i++)
        if (gWindows[i].tileData && (!sFeedbackVisible || i!=sFeedbackWindow) && i!=GetMapNamePopUpWindowId()) return FALSE;
    return TRUE;
}
static void PlayerName(u8 *out, u8 player)
{
    u32 n;
    for (n=0; n<PLAYER_NAME_LEN-1 && gSoulLink.players[player].name[n]; n++) {
        u8 c=gSoulLink.players[player].name[n];
        out[n] = c>='A' && c<='Z' ? CHAR_A+c-'A' : c>='a' && c<='z' ? CHAR_a+c-'a' : c>='0' && c<='9' ? CHAR_0+c-'0' : CHAR_SPACE;
    }
    out[n]=EOS;
    if (!n) { StringCopy(out, COMPOUND_STRING("Player ")); ConvertIntToDecimalStringN(out+7, player+1, STR_CONV_MODE_LEFT_ALIGN, 1); }
}
static void PrintNotice(const struct Notice *n)
{
    u8 text[96], name[10], location[24]; u8 *p;
    PlayerName(name,n->player);
    if (HasVerifiedOpportunity(n->route)) GetMapName(location,n->route,0);
    else StringCopy(location,n->route==255 ? COMPOUND_STRING("Starter link") : COMPOUND_STRING("Encounter link"));
    switch (n->kind) {
    case SL_NOTICE_CATCH:
        p=StringCopy(text,name); p=StringCopy(p,COMPOUND_STRING(" caught ")); p=StringCopy(p,GetSpeciesName(n->species));
        p=StringCopy(p,COMPOUND_STRING("\n")); p=StringCopy(p,location);
        if ((n->progress&255)>0) { p=StringCopy(p,COMPOUND_STRING("  ")); p=ConvertIntToDecimalStringN(p,n->progress>>8,STR_CONV_MODE_LEFT_ALIGN,1); p=StringCopy(p,COMPOUND_STRING("/")); ConvertIntToDecimalStringN(p,n->progress&255,STR_CONV_MODE_LEFT_ALIGN,1); }
        break;
    case SL_NOTICE_MISS:
        p=StringCopy(text,name); p=StringCopy(p,COMPOUND_STRING(" missed encounter\n")); p=StringCopy(p,location); StringCopy(p,COMPOUND_STRING(" link forfeited")); break;
    case SL_NOTICE_DEATH:
        p=StringCopy(text,name); p=StringCopy(p,COMPOUND_STRING("'s ")); p=StringCopy(p,n->species ? GetSpeciesName(n->species) : COMPOUND_STRING("linked Pokémon")); p=StringCopy(p,COMPOUND_STRING(" fainted\n")); StringCopy(p,n->route==255 ? COMPOUND_STRING("Starter link") : location); break;
    case SL_NOTICE_BADGE: StringCopy(text,COMPOUND_STRING("All players cleared the Gym!")); break;
    case SL_NOTICE_LOST: StringCopy(text,COMPOUND_STRING("Connection lost")); break;
    case SL_NOTICE_RECONNECT: StringCopy(text,COMPOUND_STRING("Reconnecting...")); break;
    default: StringCopy(text,COMPOUND_STRING("Soul Link synchronized")); break;
    }
    const struct WindowTemplate window = {.bg=0,.tilemapLeft=1,.tilemapTop=1,.width=28,.height=4,.paletteNum=14,.baseBlock=0x107};
    sFeedbackWindow=AddWindow(&window);
    if (sFeedbackWindow==WINDOW_NONE) { if (sFeedbackDropped!=0xFFFF) sFeedbackDropped++; return; }
    sFeedbackVisible=TRUE; sFeedbackActiveKind=n->kind;
    LoadStdWindowGfx(sFeedbackWindow,STD_WINDOW_BASE_TILE_NUM,STD_WINDOW_PALETTE_NUM*16);
    DrawStdWindowFrame(sFeedbackWindow,FALSE);
    AddTextPrinterParameterized(sFeedbackWindow,FONT_SMALL,text,2,1,TEXT_SKIP_DRAW,NULL);
    CopyWindowToVram(sFeedbackWindow,COPYWIN_FULL); sFeedbackTimer=180;
}
void SoulLinkFeedback_Update(void)
{
    u8 status, transport=gSoulLink.transportState; u32 i, best;
    SoulLinkRun_CanProgress(); // Apply the existing mirror lock before using cached UI state.
    status=SoulLinkRun_GetStatus();
    if (sRunStatus==SOULLINK_RUN_AUTHORIZED && status!=sRunStatus) { sFeedbackCount=0; SoulLinkFeedback_Hide(); }
    if (status!=SOULLINK_RUN_SOLO) {
        if (transport!=SOULLINK_TRANSPORT_CONNECTED) sNeedsSynchronized=TRUE;
        if (transport!=sTransport) {
            if (transport==SOULLINK_TRANSPORT_DISCONNECTED || transport==SOULLINK_TRANSPORT_ERROR) SoulLinkFeedback_Enqueue(SL_NOTICE_LOST,0,0,0,0);
            else if (transport==SOULLINK_TRANSPORT_CONNECTING) SoulLinkFeedback_Enqueue(SL_NOTICE_RECONNECT,0,0,0,0);
        }
        if (sNeedsSynchronized && transport==SOULLINK_TRANSPORT_CONNECTED && status==SOULLINK_RUN_AUTHORIZED
            && SoulLinkRun_CanProgress() && gSoulLink.cmdHead==gSoulLink.cmdTail) {
            SoulLinkFeedback_Enqueue(SL_NOTICE_SYNCED,0,0,0,0);sNeedsSynchronized=FALSE;
        }
    }
    sTransport=transport; sRunStatus=status;
    if (!SoulLinkFeedback_CanPresent() || IsMapNamePopupActive() || gSoulLink.cmdHead!=gSoulLink.cmdTail || SoulLink_IsWipeCheckPending()) { SoulLinkFeedback_Hide(); return; }
    if (sFeedbackVisible) { if (sFeedbackTimer && --sFeedbackTimer==0) SoulLinkFeedback_Hide(); return; }
    if (!sFeedbackCount) return;
    best=NOTICE_CAPACITY;
    for (i=0;i<sFeedbackCount;i++) {
        if (sQueue[i].kind<SL_NOTICE_LOST && (status!=SOULLINK_RUN_AUTHORIZED || !SoulLinkRun_CanProgress())) continue;
        if (best==NOTICE_CAPACITY || Priority(sQueue[i].kind)>Priority(sQueue[best].kind)) best=i;
    }
    if (best!=NOTICE_CAPACITY) { struct Notice n=sQueue[best]; Remove(best); PrintNotice(&n); }
}

#if TESTING
u8 SoulLinkFeedback_TestKind(u8 index) { return index < sFeedbackCount ? sQueue[index].kind : 0; }
#endif
