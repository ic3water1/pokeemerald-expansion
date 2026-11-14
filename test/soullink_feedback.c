#include "global.h"
#include "soullink.h"
#include "soullink_run.h"
#include "soullink_feedback.h"
#include "event_data.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "text.h"
#include "map_name_popup.h"
#include "field_message_box.h"
#include "test/test.h"
#include "constants/flags.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"
static void Prepare(void)
{
    SoulLink_Init(); SoulLinkRun_SetStatus(SOULLINK_RUN_SOLO); SoulLink_NewGame(); SoulLinkFeedback_Init();
    gMapHeader.mapType=MAP_TYPE_ROUTE; gMapHeader.regionMapSectionId=MAPSEC_ROUTE_101;
    FlagSet(FLAG_ADVENTURE_STARTED);
}
TEST("SoulLinkFeedback: pre-ball is Locked, starter receipt does not consume wild encounter")
{
    Prepare(); FlagClear(FLAG_ADVENTURE_STARTED);
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_LOCKED);
    FlagSet(FLAG_SOULLINK_STARTER_ACQUIRED); FlagSet(FLAG_ADVENTURE_STARTED);
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_AVAILABLE);
}
TEST("SoulLinkFeedback: local catches, boxed catches and confirmed forfeiture remain distinct")
{
    Prepare(); gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_ALIVE;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_USED);
    gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_BOXED;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_USED);
    gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_DEAD_FORFEITED;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_FORFEITED);
}
TEST("SoulLinkFeedback: uncertainty never claims availability or an uncommitted forfeit")
{
    Prepare(); SoulLinkRun_SetStatus(SOULLINK_RUN_AUTHORIZED); gSoulLink.transportState=SOULLINK_TRANSPORT_CONNECTED;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_AVAILABLE);
    gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_DEAD_FORFEITED;gSoulLink.eventHead=1;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_SYNCING);
    gSoulLink.eventTail=1; EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_FORFEITED);
    gSoulLink.transportState=SOULLINK_TRANSPORT_DISCONNECTED; EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_SYNCING);
}
TEST("SoulLinkFeedback: no invented town/building opportunities, verified fishing towns supported")
{
    Prepare(); gMapHeader.regionMapSectionId=MAPSEC_OLDALE_TOWN;gMapHeader.mapType=MAP_TYPE_TOWN;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_NONE);
    gMapHeader.regionMapSectionId=MAPSEC_DEWFORD_TOWN;EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_AVAILABLE);
    gMapHeader.mapType=MAP_TYPE_INDOOR;EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_NONE);
}
TEST("SoulLinkFeedback: unvalidated Route104 split remains hidden without changing saved policy")
{
    Prepare();gMapHeader.regionMapSectionId=MAPSEC_ROUTE_104;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_NONE);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_104],ROUTE_STATE_OPEN);
}
TEST("SoulLinkFeedback: Safari and cave floors do not spam repeated canonical banners; returning does")
{
    Prepare();gMapHeader.regionMapSectionId=MAPSEC_SAFARI_ZONE;EXPECT(SoulLinkFeedback_BannerEntry());SoulLinkFeedback_BannerShown();EXPECT(!SoulLinkFeedback_BannerEntry());
    gMapHeader.regionMapSectionId=MAPSEC_GRANITE_CAVE;EXPECT(SoulLinkFeedback_BannerEntry());SoulLinkFeedback_BannerShown();EXPECT(!SoulLinkFeedback_BannerEntry());
    gMapHeader.regionMapSectionId=MAPSEC_SAFARI_ZONE;EXPECT(SoulLinkFeedback_BannerEntry());
}
TEST("SoulLinkFeedback: bounded cosmetic queue preserves authority rings and deterministic priority")
{
    u32 i;Prepare();for(i=0;i<8;i++)SoulLinkFeedback_Enqueue(SL_NOTICE_CATCH,0,16,261,0x102);
    SoulLinkFeedback_Enqueue(SL_NOTICE_DEATH,1,16,261,0);EXPECT_EQ(SoulLinkFeedback_Pending(),8);EXPECT_EQ(SoulLinkFeedback_Dropped(),1);
    EXPECT_EQ(SoulLinkFeedback_TestKind(7),SL_NOTICE_DEATH);EXPECT_EQ(gSoulLink.eventHead,0);EXPECT_EQ(gSoulLink.cmdHead,0);
    for(i=0;i<100;i++)SoulLinkFeedback_Enqueue(SL_NOTICE_MISS,i%8,16,0,0);
    EXPECT_EQ(SoulLinkFeedback_Pending(),8);EXPECT_EQ(SoulLinkFeedback_Dropped(),101);
    EXPECT_EQ(gSoulLink.eventHead,0);EXPECT_EQ(gSoulLink.cmdHead,0);
}
TEST("SoulLinkFeedback: invalid cosmetic inputs cannot index player or species tables")
{
    Prepare();SoulLinkFeedback_Enqueue(99,0,16,261,0);SoulLinkFeedback_Enqueue(1,8,16,261,0);SoulLinkFeedback_Enqueue(1,0,16,0xffff,0);
    EXPECT_EQ(SoulLinkFeedback_Pending(),0);
}
TEST("SoulLinkFeedback: real overworld only; battle, menu, fade and script safe points excluded")
{
    MainCallback saved=gMain.callback2;
    Prepare();gMain.callback2=CB2_Overworld;gMain.inBattle=FALSE;gPaletteFade.active=FALSE;ScriptContext_Init();UnlockPlayerFieldControls();InitFieldMessageBox();
    EXPECT(SoulLinkFeedback_CanPresent());gMain.inBattle=TRUE;EXPECT(!SoulLinkFeedback_CanPresent());gMain.inBattle=FALSE;
    gPaletteFade.active=TRUE;EXPECT(!SoulLinkFeedback_CanPresent());gPaletteFade.active=FALSE;
    gMain.callback2=CB2_OverworldBasic;EXPECT(!SoulLinkFeedback_CanPresent());gMain.callback2=CB2_Overworld;
    LockPlayerFieldControls();EXPECT(!SoulLinkFeedback_CanPresent());UnlockPlayerFieldControls();
    gMain.callback2=saved;
}

TEST("SoulLinkFeedback: status labels fit the native 80-pixel banner")
{
    u32 i; for (i=SL_CATCH_AVAILABLE;i<=SL_CATCH_SYNCING;i++) EXPECT(GetStringWidth(FONT_SMALL,SoulLinkFeedback_StatusText(i),0)<=80);
}

TEST("SoulLinkFeedback: bounded idle and burst processing cost")
{
    u32 i, ticks, maximum=0;MainCallback saved=gMain.callback2;Prepare();gMain.callback2=NULL;
    BenchmarkStart();REG_TM3CNT=(TIMER_ENABLE|TIMER_1024CLK)<<16;
    for(i=0;i<32;i++)SoulLinkFeedback_Enqueue(SL_NOTICE_CATCH,i%8,16,261,0x102);
    SoulLinkFeedback_Update();ticks=BenchmarkStop().ticks;
    Test_MgbaPrintf("FEEDBACKPERF:burst32-and-idle:%d:1024",ticks);
    EXPECT_LT(ticks,64); // Stress-only 32-event burst, under one 4 ms worker budget.
    for(i=0;i<32;i++) {
        BenchmarkStart();REG_TM3CNT=(TIMER_ENABLE|TIMER_1024CLK)<<16;
        SoulLinkFeedback_Enqueue(SL_NOTICE_CATCH,i%8,16,261,0x102);SoulLinkFeedback_Update();
        ticks=BenchmarkStop().ticks;if(ticks>maximum)maximum=ticks;
    }
    Test_MgbaPrintf("FEEDBACKPERF:actual-one-command-idle-max:%d:1024",maximum);
    EXPECT_LT(maximum,8);
    gMain.callback2=saved;
}

TEST("SoulLinkFeedback: confirmed permanent state survives a restored pre-ball story flag")
{
    Prepare();FlagClear(FLAG_ADVENTURE_STARTED);gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_DEAD_FORFEITED;
    SoulLinkRun_SetStatus(SOULLINK_RUN_AUTHORIZED);gSoulLink.transportState=SOULLINK_TRANSPORT_CONNECTED;
    EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_FORFEITED);
}

TEST("SoulLinkFeedback: connection synchronization never confirms a pending local event")
{
    MainCallback saved=gMain.callback2;Prepare();gMain.callback2=NULL;
    SoulLinkRun_SetStatus(SOULLINK_RUN_AUTHORIZED);gSoulLink.transportState=SOULLINK_TRANSPORT_DISCONNECTED;
    SoulLinkFeedback_Update();EXPECT_EQ(SoulLinkFeedback_Pending(),1);
    gSoulLink.transportState=SOULLINK_TRANSPORT_CONNECTED;gSoulLink.eventHead=1;
    SoulLinkFeedback_Update();EXPECT_EQ(SoulLinkFeedback_Pending(),1);
    gSoulLink.eventTail=1;SoulLinkFeedback_Update();EXPECT_EQ(SoulLinkFeedback_Pending(),2);
    EXPECT_EQ(SoulLinkFeedback_TestKind(1),SL_NOTICE_SYNCED);gMain.callback2=saved;
}

TEST("SoulLinkFeedback: suppressed or cancelled banner requests never consume an entry")
{
    Prepare();FlagSet(FLAG_HIDE_MAP_NAME_POPUP);ShowMapNamePopup();FlagClear(FLAG_HIDE_MAP_NAME_POPUP);
    EXPECT(SoulLinkFeedback_BannerEntry());EXPECT(SoulLinkFeedback_BannerEntry());
    SoulLinkFeedback_BannerShown();EXPECT(!SoulLinkFeedback_BannerEntry());
}

TEST("SoulLinkFeedback: native entry via a non-banner interior resets only a different canonical area")
{
    Prepare();EXPECT(SoulLinkFeedback_BannerEntry());SoulLinkFeedback_BannerShown();
    SoulLinkFeedback_EnterArea(MAPSEC_ROUTE_101);EXPECT(!SoulLinkFeedback_BannerEntry());
    SoulLinkFeedback_EnterArea(MAPSEC_OLDALE_TOWN); // Interior does not call ShowMapNamePopup.
    SoulLinkFeedback_EnterArea(MAPSEC_ROUTE_101);EXPECT(SoulLinkFeedback_BannerEntry());
    gSoulLink.routeState[MAPSEC_ROUTE_101]=ROUTE_STATE_ALIVE;EXPECT_EQ(SoulLinkFeedback_GetCatchStatus(),SL_CATCH_USED);
}
