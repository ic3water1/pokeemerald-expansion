#include "global.h"
#include "load_save.h"
#include "save.h"
#include "soullink_run.h"
#include "soullink.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"
#include "test/test.h"

static struct SoulLinkRunIdentity TestIdentity(void)
{
    struct SoulLinkRunIdentity id = { .magic = SOULLINK_RUN_MAGIC, .saveVersion = 1, .protocolVersion = 2, .playerId = 1 };
    id.runId[0] = 7;
    id.participantId[0] = 9;
    return id;
}

TEST("RunIntegrity: CRC32 matches adapter standard vector")
{
    EXPECT_EQ(SoulLinkRun_Crc32("123456789", 9), 0xCBF43926);
    EXPECT_EQ(sizeof(struct SaveBlock3), 1552);
    EXPECT_EQ(offsetof(struct SaveBlock3, runIntegrity), 1496);
}

TEST("RunIntegrity: identities reject zero, wrong slot and protocol")
{
    struct SoulLinkRunIdentity id = TestIdentity();
    EXPECT(SoulLinkRun_Validate(&id));
    id.protocolVersion = 1;
    EXPECT(!SoulLinkRun_Validate(&id));
    id.protocolVersion = 2;
    id.playerId = 8;
    EXPECT(!SoulLinkRun_Validate(&id));
    id.playerId = 1;
    id.runId[0] = 0;
    EXPECT(!SoulLinkRun_Validate(&id));
}

TEST("RunIntegrity: solo reload remains solo without converting progress")
{
    ClearSav3();
    gSaveBlock3Ptr->soulLink.routeState[16] = 1;
    SoulLinkRun_OnLoad(SAVE_STATUS_OK);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_SOLO);
    EXPECT(SoulLinkRun_CanProgress());
    EXPECT(SoulLinkRun_PrepareSave());
    EXPECT_EQ(gSaveBlock3Ptr->soulLink.routeState[16], 1);
    EXPECT_EQ(gSaveBlock3Ptr->runIntegrity.identity.magic, 0);
}

TEST("RunIntegrity: solo ROM rejects imported multiplayer and blocks writes")
{
    ClearSav3();
    gSaveBlock3Ptr->runIntegrity.identity = TestIdentity();
    SoulLinkRun_OnLoad(SAVE_STATUS_OK);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_FOREIGN);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLinkRun_PrepareSave());
    EXPECT(!SoulLinkRun_ConfirmApplied(&gSaveBlock3Ptr->runIntegrity.identity, 1));
    gSaveBlock3Ptr->runIntegrity.identity.magic ^= 1;
    SoulLinkRun_OnLoad(SAVE_STATUS_OK);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_FOREIGN);
    EXPECT(!SoulLinkRun_PrepareSave());
    ClearSav3();
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY);
}

TEST("RunProfile: native validation stays locked until coordinator authorization")
{
    SoulLink_Init();
    ClearSav3();

    struct SoulLinkRunIdentity id;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    gSaveBlock3Ptr->runIntegrity.identity = id;
    gSaveBlock3Ptr->runIntegrity.authoritativeRevision = 2;

    SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT_EQ(gSoulLink.runStatus, SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLink_CanCatchOnCurrentMap());

    gSoulLink.lobbyRequest.requestId = 1;
    gSoulLink.transportState = SOULLINK_TRANSPORT_CONNECTED;
    // Queue CONFIRM_PLAN command with revision 3
    u16 slot = gSoulLink.cmdHead & SOULLINK_QUEUE_MASK;
    gSoulLink.cmdQueue[slot].sequenceId = 1;
    gSoulLink.cmdQueue[slot].cmdType = SOULLINK_CMD_CONFIRM_PLAN;
    gSoulLink.cmdQueue[slot].paramValue = 3;
    gSoulLink.cmdQueue[slot].paramPrimaryType = 1;
    gSoulLink.cmdQueue[slot].executionStatus = SOULLINK_CMD_STATUS_PENDING;
    gSoulLink.cmdHead++;

    // Force safe overworld conditions
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    gSoulLink.routeState[16] = ROUTE_STATE_OPEN;

    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.cmdQueue[slot].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    struct SoulLinkCommand authorize = { .sequenceId = 2, .cmdType = SOULLINK_CMD_AUTHORIZE_PLAN, .paramValue = 3, .paramPrimaryType = 1 };
    EXPECT(SoulLink_ReceiveCommand(&authorize));
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_AUTHORIZED);
    EXPECT_EQ(gSoulLink.runStatus, SOULLINK_RUN_AUTHORIZED);
    EXPECT(SoulLinkRun_CanProgress());
    EXPECT_EQ(gSaveBlock3Ptr->runIntegrity.authoritativeRevision, 3);

    // Lock resets status to RECONCILE and blocks catching again
    SoulLinkRun_Lock();
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT_EQ(gSoulLink.runStatus, SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLink_CanCatchOnCurrentMap());

    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);
}

static void ExportNativeFlash(void)
{
    static const char hex[] = "0123456789abcdef";
    u8 bytes[32];
    char line[65];
    u32 sector, offset, i;
    for (sector = 0; sector < 28; sector++)
    {
        for (offset = 0; offset < 4096; offset += 32)
        {
            ReadFlash(sector, offset, bytes, sizeof(bytes));
            for (i = 0; i < sizeof(bytes); i++)
            {
                line[i * 2] = hex[bytes[i] >> 4];
                line[i * 2 + 1] = hex[bytes[i] & 15];
            }
            line[64] = 0;
            Test_MgbaPrintf("RUNFLASH:%d:%s", sector * 4096 + offset, line);
        }
    }
}

// Run separately against an isolated test ROM whose real immutable profile has been patched.
// No test override bypasses identity validation or flash serialization.
TEST("RunProfile: fresh multiplayer requires reconciliation and survives flash reload")
{
    struct SoulLinkRunIdentity id;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    ClearSav3();
    gSaveFileStatus = SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY);
    SoulLinkRun_OnNewGame();
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLinkRun_PrepareSave());
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 7));
    gSoulLink.routeState[16] = 3;
    EXPECT_EQ(TrySavingData(SAVE_NORMAL), SAVE_STATUS_OK);
    ClearSav3();
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
    EXPECT_EQ(gSaveBlock3Ptr->runIntegrity.authoritativeRevision, 7);
    EXPECT_EQ(gSaveBlock3Ptr->soulLink.routeState[16], 3);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 7));
    ExportNativeFlash();
    SoulLinkRun_Lock();
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLinkRun_ConfirmApplied(&id, 6));
}

TEST("RunProfile: imported solo, wrong-run and corrupt flash metadata fail closed")
{
    struct SoulLinkRunIdentity id, other;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    ClearSav3();
    gSaveFileStatus = SAVE_STATUS_OK;
    SoulLinkRun_OnLoad(SAVE_STATUS_OK);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(TrySavingData(SAVE_NORMAL), SAVE_STATUS_ERROR);
    gSaveFileStatus = SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY);
    SoulLinkRun_OnNewGame();
    other = id;
    other.playerId = (id.playerId + 1) % 8;
    EXPECT(!SoulLinkRun_ConfirmApplied(&other, 0));
    other = id;
    other.runId[0] ^= 1;
    EXPECT(!SoulLinkRun_ConfirmApplied(&other, 0));
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 0));
    EXPECT(SoulLinkRun_PrepareSave());
    gSaveBlock3Ptr->soulLink.routeState[16] ^= 1;
    SoulLinkRun_OnLoad(SAVE_STATUS_OK);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_CORRUPT);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(TrySavingData(SAVE_NORMAL), SAVE_STATUS_ERROR);
}

TEST("RunProfile: adapter mirror can revoke but cannot manufacture authorization")
{
    struct SoulLinkRunIdentity id;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init(); ClearSav3();
    gSaveBlock3Ptr->runIntegrity.identity = id;
    SoulLinkRun_SetStatus(SOULLINK_RUN_SOLO);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    gSoulLink.runStatus = SOULLINK_RUN_AUTHORIZED;
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 0));
    gSoulLink.runStatus = SOULLINK_RUN_RECONCILE;
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
}

extern u32 gSoulLinkTestSaveCrcCount;
TEST("RunProfile: full link and incremental transactions calculate one finalized CRC")
{
    struct SoulLinkRunIdentity id;
    u32 before, i;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init(); ClearSav3(); gSaveFileStatus = SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY); SoulLinkRun_OnNewGame();
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 0));
    before = gSoulLinkTestSaveCrcCount;
    BenchmarkStart(); REG_TM3CNT = (TIMER_ENABLE | TIMER_1024CLK) << 16;
    EXPECT_EQ(TrySavingData(SAVE_NORMAL), SAVE_STATUS_OK);
    struct Benchmark timing = BenchmarkStop();
    Test_MgbaPrintf("REPAIRPERF:protected-normal-save:%d:1024", timing.ticks);
    BenchmarkStart(); REG_TM3CNT = (TIMER_ENABLE | TIMER_1024CLK) << 16;
    u32 checksum = SoulLinkRun_Crc32(gSaveBlock3Ptr, 1548);
    timing = BenchmarkStop();
    EXPECT_NE(checksum,0);
    Test_MgbaPrintf("REPAIRPERF:protected-crc:%d:1024", timing.ticks);
    EXPECT_EQ(gSoulLinkTestSaveCrcCount - before, 1);
    gSoulLink.routeState[16] = ROUTE_STATE_DEAD_FORFEITED;
    before = gSoulLinkTestSaveCrcCount;
    EXPECT_EQ(TrySavingData(SAVE_LINK), SAVE_STATUS_OK);
    EXPECT_EQ(gSoulLinkTestSaveCrcCount - before, 1);
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
    EXPECT_EQ(gSaveBlock3Ptr->soulLink.routeState[16], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 0));
    before = gSoulLinkTestSaveCrcCount;
    EXPECT(!LinkFullSave_Init());
    for(i=0;i<20;i++) if(LinkFullSave_WriteSector()) break;
    EXPECT_LT(i,20);
    EXPECT_EQ(gSoulLinkTestSaveCrcCount - before, 1);
    LinkFullSave_ReplaceLastSector();
    LinkFullSave_SetLastSectorSignature();
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
    EXPECT(SoulLinkRun_ConfirmApplied(&id, 0));
    before = gSoulLinkTestSaveCrcCount;
    EXPECT(!WriteSaveBlock2());
    for(i=0;i<20;i++) if(WriteSaveBlock1Sector()) break;
    EXPECT_LT(i,20);
    EXPECT_EQ(gSoulLinkTestSaveCrcCount - before, 1);
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL), SAVE_STATUS_OK);
}

TEST("RunProfile: a foreign emulator state cannot authorize or overwrite this profile")
{
    struct SoulLinkRunIdentity id;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init(); ClearSav3();
    gSaveBlock3Ptr->runIntegrity.identity = id;
    gSaveBlock3Ptr->runIntegrity.identity.participantId[0] ^= 1;
    SoulLinkRun_SetStatus(SOULLINK_RUN_AUTHORIZED);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_FOREIGN);
    EXPECT_EQ(TrySavingData(SAVE_NORMAL), SAVE_STATUS_ERROR);
    SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_ConfirmApplied(&gSaveBlock3Ptr->runIntegrity.identity, 0));
}
TEST("RunProfile: interrupted incremental transaction recovers the prior complete flash slot")
{
    struct SoulLinkRunIdentity id;
    u32 i;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init(); ClearSav3(); gSaveFileStatus=SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY); SoulLinkRun_OnNewGame();
    EXPECT(SoulLinkRun_ConfirmApplied(&id,0));
    EXPECT_EQ(TrySavingData(SAVE_NORMAL),SAVE_STATUS_OK);
    EXPECT_EQ(TrySavingData(SAVE_NORMAL),SAVE_STATUS_OK); // Fill both rotating slots first.
    gSoulLink.routeState[17]=ROUTE_STATE_DEAD_FORFEITED;
    EXPECT(!LinkFullSave_Init());
    for(i=0;i<5;i++) EXPECT(!LinkFullSave_WriteSector());
    // No final replacement/signature: emulate power interruption before commit.
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL),SAVE_STATUS_OK);
    EXPECT_EQ(gSaveBlock3Ptr->soulLink.routeState[17],ROUTE_STATE_OPEN);
    EXPECT_EQ(SoulLinkRun_GetStatus(),SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
}

TEST("RunProfile: malformed flash sector IDs fail closed without indexing outside the layout")
{
    struct SoulLinkRunIdentity id;
    u32 sector;
    memcpy(&id, (const void *)&gSoulLinkRunProfile.identity, sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init(); ClearSav3(); gSaveFileStatus=SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY); SoulLinkRun_OnNewGame();
    EXPECT(SoulLinkRun_ConfirmApplied(&id,0));
    EXPECT_EQ(TrySavingData(SAVE_NORMAL),SAVE_STATUS_OK);
    EXPECT_EQ(TrySavingData(SAVE_NORMAL),SAVE_STATUS_OK);
    for(sector=0;sector<28;sector++)
    {
        ReadFlash(sector,0,(u8 *)&gSaveDataBuffer,SECTOR_SIZE);
        gSaveDataBuffer.id=0xFFFF;
        EXPECT_EQ(ProgramFlashSectorAndVerify(sector,(u8 *)&gSaveDataBuffer),0);
    }
    EXPECT_EQ(LoadGameSave(SAVE_NORMAL),SAVE_STATUS_CORRUPT);
    EXPECT_EQ(SoulLinkRun_GetStatus(),SOULLINK_RUN_CORRUPT);
    EXPECT(!SoulLinkRun_CanProgress());
}

TEST("RunProfile: lost critical capture cannot be hidden by an empty queue or a new plan")
{
    struct SoulLinkRunIdentity id;
    memcpy(&id,(const void *)&gSoulLinkRunProfile.identity,sizeof(id));
    ASSUME(SoulLinkRun_Validate(&id));
    SoulLink_Init();ClearSav3();gSaveFileStatus=SAVE_STATUS_EMPTY;
    SoulLinkRun_OnLoad(SAVE_STATUS_EMPTY);SoulLinkRun_OnNewGame();
    gSoulLink.criticalOverflowCount=1;
    gSoulLink.eventTail=gSoulLink.eventHead;
    EXPECT(!SoulLinkRun_ConfirmApplied(&id,0));
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT_EQ(SoulLinkRun_GetStatus(),SOULLINK_RUN_CORRUPT);
    EXPECT_EQ(TrySavingData(SAVE_NORMAL),SAVE_STATUS_ERROR);
}
