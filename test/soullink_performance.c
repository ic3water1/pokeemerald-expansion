#include "global.h"
#include "soullink.h"
#include "soullink_run.h"
#include "load_save.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "test/test.h"
#include "constants/species.h"
static void StartPerf(void) { BenchmarkStart(); REG_TM3CNT = (TIMER_ENABLE | TIMER_1024CLK) << 16; }
static u32 StopPerf(void) { return BenchmarkStop().ticks; }
static void SetupPerfPC(bool8 full)
{
    struct BoxPokemon filler;
    u32 b,p; u8 location=17;
    SoulLink_Init(); SoulLink_NewGame(); SoulLinkRun_SetStatus(SOULLINK_RUN_SOLO);
    gPokemonStoragePtr=&gPokemonStorage.block; ResetPokemonStorageSystem(); ZeroPlayerPartyMons();
    CreateMon(&gParties[B_TRAINER_PLAYER][0],SPECIES_TREECKO,10,0,OTID_STRUCT_PRESET(0));
    SetMonData(&gParties[B_TRAINER_PLAYER][0],MON_DATA_MET_LOCATION,&location);
    if (full) {
        CreateBoxMon(&filler,SPECIES_TREECKO,10,0,OTID_STRUCT_PRESET(0)); SetBoxMonData(&filler,MON_DATA_MET_LOCATION,&location);
        for(b=0;b<TOTAL_BOXES_COUNT;b++) if(b!=SOULLINK_GRAVEYARD_BOX) for(p=0;p<IN_BOX_COUNT;p++) SetBoxMonAt(b,p,&filler);
    }
    SoulLink_TestSetSafeOverworldOverride(TRUE,TRUE);
}
TEST("SoulLinkPerf: idle overhead remains below half a millisecond")
{
    u32 full,i,ticks,max=0;
    for(full=0;full<2;full++) {
        SetupPerfPC(full);
        for(i=0;i<110;i++) { StartPerf(); SoulLink_ProcessOverworldCommands(); ticks=StopPerf(); if(ticks>max) max=ticks; }
    }
    Test_MgbaPrintf("REPAIRPERF:idle-max:%d:1024",max);
    EXPECT_LT(max,8); // 8 ticks = 0.488 ms at 16.777216 MHz.
    SoulLink_TestSetSafeOverworldOverride(FALSE,FALSE);
}
TEST("SoulLinkPerf: one eight and sixteen death commands keep each iteration below four milliseconds")
{
    u32 count,i,iterations,ticks,max=0,total;
    static const u8 counts[]={1,8,16};
    for(count=0;count<ARRAY_COUNT(counts);count++) {
        SetupPerfPC(TRUE); total=0; iterations=0;
        for(i=0;i<counts[count];i++) {
            struct SoulLinkCommand cmd={.sequenceId=i+1,.cmdType=SOULLINK_CMD_KILL_MON,.targetMapSec=16,.paramValue=SPECIES_TREECKO};
            EXPECT(SoulLink_ReceiveCommand(&cmd));
        }
        do {
            StartPerf(); SoulLink_ProcessOverworldCommands(); ticks=StopPerf();
            if(ticks>max) max=ticks; total+=ticks; iterations++;
            EXPECT_LT(iterations,4096);
        } while(SoulLink_GetPendingCommandCount());
        Test_MgbaPrintf("REPAIRPERF:kill-count-%d-max:%d:1024:total:%d:iterations:%d",counts[count],max,total,iterations);
        EXPECT_LT(max,65); // 65 ticks = 3.967 ms.
        EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId,counts[count]);
    }
    SoulLink_TestSetSafeOverworldOverride(FALSE,FALSE);
}

extern const u8 *SoulLink_TestFaceTiles(u8 index);
extern const u8 *GetMonIconTiles(u16 species, u32 personality);
TEST("SoulLinkPerf: ROM face bytes exactly match native cropped frames")
{
    static const u16 species[]={SPECIES_PIKACHU,SPECIES_BULBASAUR,SPECIES_CHARMANDER,SPECIES_SQUIRTLE,SPECIES_EEVEE,SPECIES_JIGGLYPUFF,SPECIES_MEOWTH,SPECIES_GENGAR,SPECIES_TREECKO,SPECIES_TORCHIC,SPECIES_MUDKIP,SPECIES_RALTS,SPECIES_LOTAD,SPECIES_SPHEAL,SPECIES_SABLEYE,SPECIES_ABSOL};
    static const u8 cutoffs[]={22,22,21,21,21,26,20,24,20,20,22,20,26,27,21,20};
    u32 i,frame,y,x,byte;
    for(i=0;i<16;i++) for(frame=0;frame<2;frame++) for(y=0;y<32;y++) for(x=0;x<4;x++) for(byte=0;byte<4;byte++) {
        u32 offset=(frame*16+(y/8)*4+x)*32+(y%8)*4+byte;
        EXPECT_EQ(SoulLink_TestFaceTiles(i)[offset],y>cutoffs[i]?0:GetMonIconTiles(species[i],0)[offset]);
    }
}
TEST("SoulLinkPerf: occupied matching PC and full Graveyard remain bounded and retain deaths")
{
    struct BoxPokemon filler;
    struct SoulLinkCommand cmd={.sequenceId=1,.cmdType=SOULLINK_CMD_KILL_MON,.targetMapSec=16};
    u32 box,pos,ticks,max=0,iterations=0; u8 location=16;
    SetupPerfPC(TRUE);
    CreateBoxMon(&filler,SPECIES_TREECKO,10,0,OTID_STRUCT_PRESET(0));
    SetBoxMonData(&filler,MON_DATA_MET_LOCATION,&location);
    for(box=0;box<TOTAL_BOXES_COUNT;box++) for(pos=0;pos<IN_BOX_COUNT;pos++) SetBoxMonAt(box,pos,&filler);
    EXPECT(SoulLink_ReceiveCommand(&cmd));
    do {
        StartPerf(); SoulLink_ProcessOverworldCommands(); ticks=StopPerf();
        if(ticks>max)max=ticks;
        EXPECT_LT(++iterations,256);
    } while(SoulLink_GetPendingCommandCount());
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus,SOULLINK_CMD_STATUS_DEFERRED);
    for(box=0;box<TOTAL_BOXES_COUNT;box++) if(box!=SOULLINK_GRAVEYARD_BOX) for(pos=0;pos<IN_BOX_COUNT;pos++) EXPECT(GetBoxedMonPtr(box,pos)->isDead);
    EXPECT_LT(max,65);
    Test_MgbaPrintf("REPAIRPERF:kill-full-graveyard-max:%d:1024:iterations:%d",max,iterations);
    // Free one space; bounded idle recovery must eventually transfer one retained death.
    ZeroBoxMonAt(SOULLINK_GRAVEYARD_BOX,0);
    for(pos=0;pos<110;pos++) SoulLink_ProcessOverworldCommands();
    EXPECT(GetBoxedMonPtr(SOULLINK_GRAVEYARD_BOX,0)->isDead);
    EXPECT(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX,0,MON_DATA_SPECIES)!=SPECIES_NONE);
    SoulLink_TestSetSafeOverworldOverride(FALSE,FALSE);
}

extern bool8 SoulLink_TestFaceVram(u16 *highWater);
TEST("SoulLinkPerf: native icon animation and four selected faces preserve complete VRAM frames")
{
    u16 highWater;
    EXPECT(SoulLink_TestFaceVram(&highWater));
    EXPECT(highWater <= 56);
    Test_MgbaPrintf("REPAIRPERF:sprite-copy-high-water:%d:64",highWater);
}
