#include "global.h"
#include "load_save.h"
#include "save.h"
#include "soullink_run.h"
#include "soullink.h"
#include "gba/flash_internal.h"
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

TEST("RunIntegrity: progression gate blocks catching and overworld commands execute CONFIRM_PLAN")
{
    SoulLink_Init();
    ClearSav3();

    struct SoulLinkRunIdentity id = TestIdentity();
    gSaveBlock3Ptr->runIntegrity.identity = id;
    gSaveBlock3Ptr->runIntegrity.authoritativeRevision = 2;

    SoulLinkRun_SetStatus(SOULLINK_RUN_RECONCILE);
    EXPECT_EQ(SoulLinkRun_GetStatus(), SOULLINK_RUN_RECONCILE);
    EXPECT_EQ(gSoulLink.runStatus, SOULLINK_RUN_RECONCILE);
    EXPECT(!SoulLinkRun_CanProgress());
    EXPECT(!SoulLink_CanCatchOnCurrentMap());

    // Queue CONFIRM_PLAN command with revision 3
    u16 slot = gSoulLink.cmdHead & SOULLINK_QUEUE_MASK;
    gSoulLink.cmdQueue[slot].sequenceId = 1;
    gSoulLink.cmdQueue[slot].cmdType = SOULLINK_CMD_CONFIRM_PLAN;
    gSoulLink.cmdQueue[slot].paramValue = 3;
    gSoulLink.cmdQueue[slot].executionStatus = SOULLINK_CMD_STATUS_PENDING;
    gSoulLink.cmdHead++;

    // Force safe overworld conditions
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    gSoulLink.routeState[16] = ROUTE_STATE_OPEN;

    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.cmdQueue[slot].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
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
