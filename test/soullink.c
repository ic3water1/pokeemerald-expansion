#include "global.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "battle.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "event_data.h"
#include "load_save.h"
#include "save.h"
#include "soullink.h"
#include "test/test.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"

TEST("SoulLink: 32-bit type mask correctly stores all types up to TYPE_STELLAR without overflow")
{
    SoulLink_Init();
    SoulLink_NewGame();

    // Verify type bits can be set for high index types (Fairy = 19, Stellar = 20)
    gSoulLink.routeTypesMask[0] |= SOULLINK_TYPE_BIT(TYPE_FAIRY);
    gSoulLink.routeTypesMask[0] |= SOULLINK_TYPE_BIT(TYPE_STELLAR);
    gSoulLink.routeTypesMask[0] |= SOULLINK_TYPE_BIT(TYPE_FIRE);

    EXPECT(gSoulLink.routeTypesMask[0] & SOULLINK_TYPE_BIT(TYPE_FAIRY));
    EXPECT(gSoulLink.routeTypesMask[0] & SOULLINK_TYPE_BIT(TYPE_STELLAR));
    EXPECT(gSoulLink.routeTypesMask[0] & SOULLINK_TYPE_BIT(TYPE_FIRE));
    EXPECT(!(gSoulLink.routeTypesMask[0] & SOULLINK_TYPE_BIT(TYPE_WATER)));
}

TEST("SoulLink: Catch restrictions and first encounter tracking")
{
    SoulLink_Init();
    SoulLink_NewGame();
    gMapHeader.regionMapSectionId = MAPSEC_ROUTE_101;

    // Route 101 should initially be open for catching
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], ROUTE_STATE_OPEN);
    EXPECT_EQ(SoulLink_CanCatchOnCurrentMap(), TRUE);

    // Wild battle starts: mark active encounter
    SoulLink_OnWildBattleStart();
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], ROUTE_STATE_ENCOUNTER_ACTIVE);
    EXPECT_EQ(SoulLink_CanCatchOnCurrentMap(), TRUE);

    // Successful catch
    SoulLink_OnWildCatchSuccess(SPECIES_POOCHYENA);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], ROUTE_STATE_ALIVE);
    EXPECT_EQ(gSoulLink.routeSpecies[MAPSEC_ROUTE_101][0], SPECIES_POOCHYENA);

    // Subsequent catches on this route must now be blocked
    EXPECT_EQ(SoulLink_CanCatchOnCurrentMap(), FALSE);
}

TEST("SoulLink: Failed first encounter forfeits route catch")
{
    SoulLink_Init();
    SoulLink_NewGame();
    gMapHeader.regionMapSectionId = MAPSEC_ROUTE_102;

    EXPECT_EQ(SoulLink_CanCatchOnCurrentMap(), TRUE);
    SoulLink_OnWildBattleStart();
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_ENCOUNTER_ACTIVE);

    // Wild Pokemon fainted or player ran away
    SoulLink_OnWildEncounterFailed();
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);

    // Catches must be permanently disallowed on this route
    EXPECT_EQ(SoulLink_CanCatchOnCurrentMap(), FALSE);
}

TEST("SoulLink: Permanent deaths move fainted Pokemon to Graveyard box and compact party")
{
    u32 i;
    u8 metLoc = MAPSEC_ROUTE_103;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();
    gSoulLink.routeState[metLoc] = ROUTE_STATE_ALIVE;

    // Clear party and storage
    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    // Create 2 Pokemon
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);

    CreateMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_ZIGZAGOON, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][1]);

    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 2);

    // Mon 0 faints in battle
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), 0);

    // Return to overworld and process fainted mons
    SoulLink_ProcessFaintedMonsPostBattle();

    // Mon 0 must have been removed and party compacted (Zigzagoon is now slot 0)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_ZIGZAGOON);
    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 1);

    // Route 103 must be marked dead/forfeited
    EXPECT_EQ(gSoulLink.routeState[metLoc], ROUTE_STATE_DEAD_FORFEITED);
}

TEST("SoulLink: Gym progression locks rely on maps and are immune to stale script variables")
{
    SoulLink_Init();
    SoulLink_NewGame();

    // Contaminate gSpecialVar_0x8004 with a stale value from a previous script
    gSpecialVar_0x8004 = 99;

    // Stage 0: Initial level cap is 15 (Roxanne)
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 15);

    // In Rustboro Gym: Roxanne requires Stage 0 -> must be TRUE despite stale var
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_RUSTBORO_CITY_GYM);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_RUSTBORO_CITY_GYM);
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // In Dewford Gym: Brawly requires Stage 1 -> must be FALSE at Stage 0
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_DEWFORD_TOWN_GYM);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_DEWFORD_TOWN_GYM);
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);

    // Earn Stone Badge
    FlagSet(FLAG_BADGE01_GET);
    SoulLink_OnBadgeEarned();
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 19);

    // Dewford Gym now unlocked
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // Mauville Gym requires Stage 2 -> still blocked
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_MAUVILLE_CITY_GYM);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_MAUVILLE_CITY_GYM);
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);

    // Direct stage query
    EXPECT_EQ(SoulLink_IsGymStageUnlocked(1), TRUE);
    EXPECT_EQ(SoulLink_IsGymStageUnlocked(2), FALSE);
}

TEST("SoulLink: Full save-file restart persistence via SaveBlock3 and load hooks")
{
    SoulLink_Init();
    SoulLink_NewGame();

    // Mark route 101 alive with Poochyena and Dark type
    gSoulLink.routeState[MAPSEC_ROUTE_101] = ROUTE_STATE_ALIVE;
    gSoulLink.routeSpecies[MAPSEC_ROUTE_101][0] = SPECIES_POOCHYENA;
    gSoulLink.routeTypesMask[MAPSEC_ROUTE_101] = SOULLINK_TYPE_BIT(TYPE_DARK);

    // Mark route 102 dead
    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_DEAD_FORFEITED;

    // Advance gym stage to 3
    gSoulLink.unlockedGymStage = 3;

    // Execute standard save serialization hook
    CopyPartyAndObjectsToSave();

    // Simulate complete console reboot: wipe EWRAM mailbox entirely
    memset(&gSoulLink, 0, sizeof(gSoulLink));
    EXPECT_EQ(gSoulLink.magic, 0);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 0);

    // Boot hook: SoulLink_Init() sets magic & transport without overwriting save data
    SoulLink_Init();
    EXPECT_EQ(gSoulLink.magic, SOULLINK_MAGIC);

    // Game load hook: CopyPartyAndObjectsFromSave() restores persistent Nuzlocke state
    CopyPartyAndObjectsFromSave();

    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], ROUTE_STATE_ALIVE);
    EXPECT_EQ(gSoulLink.routeSpecies[MAPSEC_ROUTE_101][0], SPECIES_POOCHYENA);
    EXPECT(gSoulLink.routeTypesMask[MAPSEC_ROUTE_101] & SOULLINK_TYPE_BIT(TYPE_DARK));
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 3);
}

TEST("SoulLink: Graveyard-full safety prevents permanent Pokemon deletion")
{
    u32 i;
    u8 metLoc = MAPSEC_ROUTE_103;
    u16 zeroHp = 0;
    struct BoxPokemon fillerMon;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Fill all 30 spots of the Graveyard box (Box 13)
    CreateBoxMon(&fillerMon, SPECIES_PIDGEY, 5, 0, OTID_STRUCT_PRESET(0));
    for (i = 0; i < IN_BOX_COUNT; i++)
    {
        SetBoxMonAt(SOULLINK_GRAVEYARD_BOX, i, &fillerMon);
    }
    EXPECT_EQ(GetFirstFreeBoxSpot(SOULLINK_GRAVEYARD_BOX), -1);

    // Create party with fainted Treecko
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);

    gSoulLink.routeState[metLoc] = ROUTE_STATE_ALIVE;

    // Process post-battle deaths when Graveyard is full
    SoulLink_ProcessFaintedMonsPostBattle();

    // Treecko MUST NOT be deleted from the cartridge (party mon remains intact)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_TREECKO);

    // Route must still be marked dead
    EXPECT_EQ(gSoulLink.routeState[metLoc], ROUTE_STATE_DEAD_FORFEITED);
}

TEST("SoulLink: Catch validation guard and invalid catches cannot enter party")
{
    struct Pokemon wildMon;
    u8 partyCountBefore;

    SoulLink_Init();
    SoulLink_NewGame();

    gMapHeader.regionMapSectionId = MAPSEC_ROUTE_102;
    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_DEAD_FORFEITED;

    // Attempt to register a catch on a dead route
    SoulLink_OnWildCatchSuccess(SPECIES_RALTS);

    // Route must remain DEAD_FORFEITED and not change to ALIVE
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);

    // Create captured wild mon
    partyCountBefore = CalculatePlayerPartyCount();
    CreateMon(&wildMon, SPECIES_RALTS, 4, 0, OTID_STRUCT_PRESET(0));

    // GiveCapturedMonToPlayer must reject invalid catch
    EXPECT_EQ(GiveCapturedMonToPlayer(&wildMon), MON_CANT_GIVE);

    // Usable party must be completely unchanged
    EXPECT_EQ(CalculatePlayerPartyCount(), partyCountBefore);
}

TEST("SoulLink: Stable per-Pokemon encounter identity without species matching")
{
    u8 locNorth = ENCOUNTER_ID_ROUTE_104_NORTH;
    u8 locSouth = ENCOUNTER_ID_ROUTE_104_SOUTH;
    u16 grovyle = SPECIES_GROVYLE;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Setup routes
    gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH] = ROUTE_STATE_ALIVE;
    gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_SOUTH] = ROUTE_STATE_ALIVE;

    // Mon 0: Caught on Route 104 North as Treecko, evolved into Grovyle
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 16, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &locNorth);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES, &grovyle); // Evolved!

    // Mon 1: Caught on Route 104 South (Zigzagoon)
    ZeroMonData(&gParties[B_TRAINER_PLAYER][1]);
    CreateMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_ZIGZAGOON, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][1]);
    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_MET_LOCATION, &locSouth);

    // Grovyle faints in battle
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);

    SoulLink_ProcessFaintedMonsPostBattle();

    // Route 104 North must be marked dead, despite evolving away from registered species
    EXPECT_EQ(gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH], ROUTE_STATE_DEAD_FORFEITED);

    // Route 104 South remains alive
    EXPECT_EQ(gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_SOUTH], ROUTE_STATE_ALIVE);
}

TEST("SoulLink: Record permanent death at actual faint event and prevent revive bypass")
{
    u8 metLoc = MAPSEC_ROUTE_103;
    u16 reviveHp = 50;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    gSoulLink.routeState[metLoc] = ROUTE_STATE_ALIVE;

    // Create party with alive Treecko in slot 0
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);

    // Mon 0 faints during battle -> actual faint event hook executes
    SoulLink_RecordFaintEvent(0);

    // Route 103 is immediately marked dead
    EXPECT_EQ(gSoulLink.routeState[metLoc], ROUTE_STATE_DEAD_FORFEITED);

    // Player attempts to revive Treecko mid-battle
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &reviveHp);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), 50);

    // Post-battle processing executes
    SoulLink_ProcessFaintedMonsPostBattle();

    // Revive is voided: Treecko is permanently transferred to Graveyard box and slot 0 is cleared
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_NONE);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SPECIES), SPECIES_TREECKO);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_HP), 0);
}

TEST("SoulLink: Graveyard PC storage blocks multi-selection operations with active group")
{
    struct Pokemon mon1, mon2;
    SoulLink_Init();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    CreateMon(&mon1, SPECIES_POOCHYENA, 5, 0, OTID_STRUCT_PRESET(0));
    CreateMon(&mon2, SPECIES_ZIGZAGOON, 5, 0, OTID_STRUCT_PRESET(0));

    // Setup an actual active multi-selection group of 2 Pokémon spanning columns 0-1, row 0
    Test_SetupMultiMoveSelection(0, 0, 2, 1, &mon1.box, &mon2.box);

    // In normal Box 0 with empty spots: MultiMove_CanPlaceSelection must return TRUE
    SetCurrentBox(0);
    EXPECT_EQ(StorageGetCurrentBox(), 0);
    EXPECT_EQ(MultiMove_CanPlaceSelection(), TRUE);

    // In Graveyard Box (Box 13): MultiMove_CanPlaceSelection must return FALSE even with empty spots!
    SetCurrentBox(SOULLINK_GRAVEYARD_BOX);
    EXPECT_EQ(StorageGetCurrentBox(), SOULLINK_GRAVEYARD_BOX);
    EXPECT_EQ(MultiMove_CanPlaceSelection(), FALSE);

    // Attempting to place into Graveyard box is blocked
    MultiMove_SetPlacedMonData();
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SANITY_HAS_SPECIES), FALSE);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 1, MON_DATA_SANITY_HAS_SPECIES), FALSE);

    Test_TeardownMultiMove();
}

TEST("SoulLink: Early rival battles and battle callbacks process faint events exactly once")
{
    u8 metLoc = MAPSEC_ROUTE_103;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    gSoulLink.routeState[metLoc] = ROUTE_STATE_ALIVE;

    // Party with 1 Treecko in slot 0
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);

    // Treecko faints during early rival battle
    SoulLink_RecordFaintEvent(0);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);

    // Post-battle processing executes
    SoulLink_ProcessFaintedMonsPostBattle();

    // Death was processed and route is dead
    EXPECT_EQ(gSoulLink.routeState[metLoc], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SPECIES), SPECIES_TREECKO);

    // Mask is now 0. Setup an unrelated new battle with a fresh Pokemon in slot 0
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_MUDKIP, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP) > 0, TRUE);

    // Calling post-battle processing on unrelated battle MUST NOT kill Mudkip
    SoulLink_ProcessFaintedMonsPostBattle();
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_MUDKIP);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP) > 0, TRUE);

    // ClearFaintEvents guarantees safe battle init
    SoulLink_RecordFaintEvent(0);
    SoulLink_ClearFaintEvents();
    SoulLink_ProcessFaintedMonsPostBattle();
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_MUDKIP);
}

TEST("SoulLink: Usable Pokemon count across party and PC handles party wipes safely")
{
    u32 i;
    struct Pokemon boxedMon;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    // Initial state: 0 in party, 0 in PC
    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 0);
    EXPECT_EQ(SoulLink_GetLivingBoxPokemonCount(), 0);
    EXPECT_EQ(SoulLink_GetTotalUsablePokemonCount(), 0);
    EXPECT_EQ(SoulLink_IsPartyWiped(), TRUE);

    // Add 1 boxed Pokemon in normal Box 0
    CreateMon(&boxedMon, SPECIES_ZIGZAGOON, 5, 0, OTID_STRUCT_PRESET(0));
    SetBoxMonAt(0, 0, &boxedMon.box);

    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 0);
    EXPECT_EQ(SoulLink_GetLivingBoxPokemonCount(), 1);
    EXPECT_EQ(SoulLink_GetTotalUsablePokemonCount(), 1);

    // Pokémon in Graveyard box (Box 13) must NOT be counted as living/usable
    SetBoxMonAt(SOULLINK_GRAVEYARD_BOX, 0, &boxedMon.box);
    EXPECT_EQ(SoulLink_GetLivingBoxPokemonCount(), 1); // Still 1 from Box 0

    // Add 1 party Pokemon
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 1);
    EXPECT_EQ(SoulLink_GetTotalUsablePokemonCount(), 2);
    EXPECT_EQ(SoulLink_IsPartyWiped(), FALSE);
}

TEST("SoulLink: Actual in-game flash save and restore via SaveBlock3 sectors")
{
    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Populate distinct Nuzlocke state
    gSoulLink.routeState[MAPSEC_ROUTE_101] = ROUTE_STATE_ALIVE;
    gSoulLink.routeSpecies[MAPSEC_ROUTE_101][0] = SPECIES_POOCHYENA;
    gSoulLink.routeTypesMask[MAPSEC_ROUTE_101] = SOULLINK_TYPE_BIT(TYPE_DARK);

    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_DEAD_FORFEITED;
    gSoulLink.routeSpecies[MAPSEC_ROUTE_102][0] = SPECIES_ZIGZAGOON;
    gSoulLink.routeTypesMask[MAPSEC_ROUTE_102] = SOULLINK_TYPE_BIT(TYPE_NORMAL);

    gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH] = ROUTE_STATE_ALIVE;
    gSoulLink.routeSpecies[ENCOUNTER_ID_ROUTE_104_NORTH][0] = SPECIES_TAILLOW;
    gSoulLink.routeTypesMask[ENCOUNTER_ID_ROUTE_104_NORTH] = SOULLINK_TYPE_BIT(TYPE_NORMAL) | SOULLINK_TYPE_BIT(TYPE_FLYING);

    gSoulLink.unlockedGymStage = 3;

    // Detect flash memory in emulator
    CheckForFlashMemory();
    EXPECT_EQ(gFlashMemoryPresent, TRUE);

    // Perform standard in-game flash save (writes to all 14 flash sectors)
    Save_ResetSaveCounters();
    u8 saveStatus = TrySavingData(SAVE_NORMAL);
    EXPECT_EQ(saveStatus, SAVE_STATUS_OK);

    // Completely wipe gSoulLink, SaveBlock3, and RAM save locations
    memset(&gSoulLink, 0, sizeof(gSoulLink));
    memset(&gSaveblock3, 0, sizeof(gSaveblock3));
    EXPECT_EQ(gSoulLink.magic, 0);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 0);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], 0);

    // Re-initialize boot state (without NewGame wipe)
    SoulLink_Init();
    EXPECT_EQ(gSoulLink.magic, SOULLINK_MAGIC);

    // Load game from flash memory (reads all 14 flash sectors)
    u8 loadStatus = LoadGameSave(SAVE_NORMAL);
    EXPECT_EQ(loadStatus, SAVE_STATUS_OK);

    // Verify all encounters, deaths, and Gym progression were restored from flash
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_101], ROUTE_STATE_ALIVE);
    EXPECT_EQ(gSoulLink.routeSpecies[MAPSEC_ROUTE_101][0], SPECIES_POOCHYENA);
    EXPECT(gSoulLink.routeTypesMask[MAPSEC_ROUTE_101] & SOULLINK_TYPE_BIT(TYPE_DARK));

    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT_EQ(gSoulLink.routeSpecies[MAPSEC_ROUTE_102][0], SPECIES_ZIGZAGOON);

    EXPECT_EQ(gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH], ROUTE_STATE_ALIVE);
    EXPECT_EQ(gSoulLink.routeSpecies[ENCOUNTER_ID_ROUTE_104_NORTH][0], SPECIES_TAILLOW);
    EXPECT(gSoulLink.routeTypesMask[ENCOUNTER_ID_ROUTE_104_NORTH] & SOULLINK_TYPE_BIT(TYPE_FLYING));

    EXPECT_EQ(gSoulLink.unlockedGymStage, 3);
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 29);
}

TEST("SoulLink: Follower NPC battle faints remap to true party slots and process before healing")
{
    u32 i;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Set up full 4-mon player party in gSaveBlock1Ptr->playerParty
    gSaveBlock1Ptr->playerPartyCount = 4;
    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gSaveBlock1Ptr->playerParty[i]);

    CreateMon(&gSaveBlock1Ptr->playerParty[0], SPECIES_MUDKIP, 10, 0, OTID_STRUCT_PRESET(0));
    CreateMon(&gSaveBlock1Ptr->playerParty[1], SPECIES_POOCHYENA, 8, 0, OTID_STRUCT_PRESET(0));
    CreateMon(&gSaveBlock1Ptr->playerParty[2], SPECIES_WURMPLE, 5, 0, OTID_STRUCT_PRESET(0)); // Non-participating mon
    CreateMon(&gSaveBlock1Ptr->playerParty[3], SPECIES_ZIGZAGOON, 7, 0, OTID_STRUCT_PRESET(0));

    for (i = 0; i < 4; i++)
        CalculateMonStats(&gSaveBlock1Ptr->playerParty[i]);

    // Zero out frontier selected party array to ensure clean test isolation
    for (i = 0; i < MAX_FRONTIER_PARTY_SIZE; i++)
        gSaveBlock2Ptr->frontier.selectedPartyMons[i] = 0;

    // Follower battle selects slots 0 (Mudkip), 1 (Poochyena), and 3 (Zigzagoon)
    // Wurmple at slot 2 was NOT selected
    gSaveBlock2Ptr->frontier.selectedPartyMons[0] = 1; // 1-based: slot 0
    gSaveBlock2Ptr->frontier.selectedPartyMons[1] = 2; // 1-based: slot 1
    gSaveBlock2Ptr->frontier.selectedPartyMons[2] = 4; // 1-based: slot 3

    // Set up in-battle player party (reduced to 3 selected mons)
    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    gParties[B_TRAINER_PLAYER][0] = gSaveBlock1Ptr->playerParty[0]; // Mudkip (HP > 0)
    gParties[B_TRAINER_PLAYER][1] = gSaveBlock1Ptr->playerParty[1]; // Poochyena (HP > 0)
    gParties[B_TRAINER_PLAYER][2] = gSaveBlock1Ptr->playerParty[3]; // Zigzagoon (fainted!)
    SetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_HP, &zeroHp);

    // Record in-battle faint event on battle slot 2 (Zigzagoon)
    SoulLink_RecordFaintEvent(2);

    // Call follower battle end handler
    SoulLink_HandleFollowerBattlePartyEnd();

    // Verify:
    // 1. Zigzagoon (original slot 3) was moved to Graveyard Box (Box 13, spot 0)
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SPECIES), SPECIES_ZIGZAGOON);

    // 2. The player's restored party was compacted from 4 to 3 mons
    // Slot 0: Mudkip (alive)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_MUDKIP);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP) > 0, TRUE);

    // Slot 1: Poochyena (alive)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_SPECIES), SPECIES_POOCHYENA);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_HP) > 0, TRUE);

    // Slot 2: Wurmple (alive! NEVER moved to Graveyard, despite being in slot 2 of original party)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_SPECIES), SPECIES_WURMPLE);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_HP) > 0, TRUE);

    // Slot 3: Empty (was Zigzagoon, now compacted)
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][3], MON_DATA_SPECIES), SPECIES_NONE);

    // Box 13 has only 1 mon (Zigzagoon)
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 1, MON_DATA_SPECIES), SPECIES_NONE);
}

TEST("SoulLink: Mailbox layout, binary offsets, alignment, and total structure size assertions")
{
    EXPECT_EQ(sizeof(struct SoulLinkMailbox), SOULLINK_MAILBOX_TOTAL_SIZE);
    EXPECT_EQ(sizeof(struct SoulLinkMailbox), 5080);
    EXPECT_EQ(sizeof(struct SoulLinkEvent), 12);
    EXPECT_EQ(sizeof(struct SoulLinkCommand), 12);
    EXPECT_EQ(sizeof(struct SoulLinkLobbyRequest), 24);
    EXPECT_EQ(sizeof(struct SoulLinkLobbyResponse), 36);
    EXPECT_EQ(sizeof(struct PlayerSyncInfo), 16);

    EXPECT_EQ(offsetof(struct SoulLinkMailbox, magic), SOULLINK_OFFSET_MAGIC);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, protocolVersion), SOULLINK_OFFSET_PROTOCOL_VERSION);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, capabilities), SOULLINK_OFFSET_CAPABILITIES);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, sessionEpoch), SOULLINK_OFFSET_SESSION_EPOCH);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, transportState), SOULLINK_OFFSET_TRANSPORT_STATE);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, mailboxFlags), SOULLINK_OFFSET_MAILBOX_FLAGS);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, eventDropCount), SOULLINK_OFFSET_EVENT_DROP_COUNT);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, criticalOverflowCount), SOULLINK_OFFSET_CRITICAL_OVERFLOW);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, lastAckedEventSequenceId), SOULLINK_OFFSET_LAST_ACKED_EVENT);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, eventHead), SOULLINK_OFFSET_EVENT_HEAD);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, eventTail), SOULLINK_OFFSET_EVENT_TAIL);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, eventQueue), SOULLINK_OFFSET_EVENT_QUEUE);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, cmdHead), SOULLINK_OFFSET_CMD_HEAD);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, cmdTail), SOULLINK_OFFSET_CMD_TAIL);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, lastExecutedCmdSequenceId), SOULLINK_OFFSET_LAST_EXEC_CMD);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, cmdDropCount), SOULLINK_OFFSET_CMD_DROP_COUNT);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, cmdQueue), SOULLINK_OFFSET_CMD_QUEUE);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, lobbyRequest), SOULLINK_OFFSET_LOBBY_REQUEST);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, lobbyResponse), SOULLINK_OFFSET_LOBBY_RESPONSE);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, playerCount), SOULLINK_OFFSET_PLAYER_COUNT);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, unlockedGymStage), SOULLINK_OFFSET_UNLOCKED_GYM);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, routeState), SOULLINK_OFFSET_ROUTE_STATE);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, routeSpecies), SOULLINK_OFFSET_ROUTE_SPECIES);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, routeTypesMask), SOULLINK_OFFSET_ROUTE_TYPES_MASK);
    EXPECT_EQ(offsetof(struct SoulLinkMailbox, players), SOULLINK_OFFSET_PLAYERS);
}

TEST("SoulLink: Outgoing event ring buffer FIFO ordering and bridge sequence acknowledgments")
{
    struct SoulLinkEvent evt;

    SoulLink_Init();
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);

    // Push 4 events
    SoulLink_EmitEvent(SOULLINK_EVENT_CAUGHT, MAPSEC_ROUTE_101, SPECIES_TREECKO, TYPE_GRASS, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, MAPSEC_ROUTE_102, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MISSED, MAPSEC_ROUTE_102, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_BADGE, MAPSEC_RUSTBORO_CITY, 0, 0, 1);

    EXPECT_EQ(SoulLink_GetPendingEventCount(), 4);

    // Pop first event: CAUGHT Treecko
    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 1);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_CAUGHT);
    EXPECT_EQ(evt.species, SPECIES_TREECKO);
    EXPECT_EQ(evt.mapSec, MAPSEC_ROUTE_101);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 3);
    EXPECT_EQ(gSoulLink.lastAckedEventSequenceId, 1);

    // Pop second event: MAP_CHANGE
    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 2);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_MAP_CHANGE);
    EXPECT_EQ(evt.mapSec, MAPSEC_ROUTE_102);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 2);

    // Bridge acknowledges remaining events up to sequence 4
    SoulLink_AcknowledgeEvents(4);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);
    EXPECT_EQ(gSoulLink.lastAckedEventSequenceId, 4);

    // Empty queue pop returns FALSE
    EXPECT_EQ(SoulLink_PopEvent(&evt), FALSE);
}

TEST("SoulLink: Multiple in-battle faints queued reliably in FIFO sequence before bridge poll")
{
    u32 i;
    u8 loc1 = MAPSEC_ROUTE_101;
    u8 loc2 = MAPSEC_ROUTE_102;
    u8 loc3 = MAPSEC_ROUTE_103;
    u16 zeroHp = 0;
    struct SoulLinkEvent evt;

    SoulLink_Init();
    SoulLink_NewGame();

    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 10, 0, OTID_STRUCT_PRESET(0));
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &loc1);

    CreateMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_MUDKIP, 10, 0, OTID_STRUCT_PRESET(0));
    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_MET_LOCATION, &loc2);

    CreateMon(&gParties[B_TRAINER_PLAYER][2], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    SetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_MET_LOCATION, &loc3);

    // Simulate 3 faints occurring during single battle before any bridge polling
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);
    SoulLink_RecordFaintEvent(0);

    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_HP, &zeroHp);
    SoulLink_RecordFaintEvent(1);

    SetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_HP, &zeroHp);
    SoulLink_RecordFaintEvent(2);

    // Queue must contain all 3 faint events with critical flag
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 3);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_FAINTED);
    EXPECT_EQ(evt.slotIndex, 0);
    EXPECT_EQ(evt.species, SPECIES_TORCHIC);
    EXPECT_EQ(evt.mapSec, loc1);
    EXPECT(evt.flags & SOULLINK_EVT_FLAG_CRITICAL);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_FAINTED);
    EXPECT_EQ(evt.slotIndex, 1);
    EXPECT_EQ(evt.species, SPECIES_MUDKIP);
    EXPECT_EQ(evt.mapSec, loc2);
    EXPECT(evt.flags & SOULLINK_EVT_FLAG_CRITICAL);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_FAINTED);
    EXPECT_EQ(evt.slotIndex, 2);
    EXPECT_EQ(evt.species, SPECIES_TREECKO);
    EXPECT_EQ(evt.mapSec, loc3);
    EXPECT(evt.flags & SOULLINK_EVT_FLAG_CRITICAL);

    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);

    SoulLink_ClearFaintEvents();
}

TEST("SoulLink: Event queue-full handling drops non-critical events and flags reconciliation for critical events")
{
    u32 i;
    struct SoulLinkEvent evt;

    SoulLink_Init();
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);

    // Fill queue with 16 non-critical events (e.g. MAP_CHANGE)
    for (i = 0; i < SOULLINK_EVENT_QUEUE_SIZE; i++)
    {
        bool8 pushed = SoulLink_PushEvent(SOULLINK_EVENT_MAP_CHANGE, (u8)i, 0, 0, 0, 0);
        EXPECT_EQ(pushed, TRUE);
    }
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    EXPECT_EQ(SoulLink_GetEventDropCount(), 0);

    // 17th non-critical event must be DROPPED
    bool8 dropPushed = SoulLink_PushEvent(SOULLINK_EVENT_MAP_CHANGE, 99, 0, 0, 0, 0);
    EXPECT_EQ(dropPushed, FALSE);
    EXPECT_EQ(SoulLink_GetEventDropCount(), 1);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_QUEUE_OVERFLOW);

    // A CRITICAL event (e.g. FAINTED) on a full queue must NOT advance bridge-owned eventTail
    // Instead, criticalOverflowCount increments and SOULLINK_FLAG_RECONCILIATION_REQUIRED is set
    bool8 critPushed = SoulLink_PushEvent(SOULLINK_EVENT_FAINTED, MAPSEC_ROUTE_104, SPECIES_PIKACHU, TYPE_ELECTRIC, 0, SOULLINK_EVT_FLAG_CRITICAL);
    EXPECT_EQ(critPushed, FALSE);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    EXPECT_EQ(gSoulLink.eventTail, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 1);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_CRITICAL_OVERFLOW);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_DIRTY_STATE);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // Drain events to verify the 16 original events are safely preserved in FIFO order
    for (i = 0; i < 16; i++)
    {
        EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
        EXPECT_EQ(evt.eventType, SOULLINK_EVENT_MAP_CHANGE);
        EXPECT_EQ(evt.mapSec, (u8)i);
    }
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);
}

TEST("SoulLink: 16-bit sequence number wraparound across 65535 skips 0 and preserves order")
{
    struct SoulLinkEvent evt;

    SoulLink_Init();
    SoulLink_TestSetNextEventSequenceId(65534);

    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 1, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 2, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 3, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 4, 0, 0, 0);

    EXPECT_EQ(SoulLink_GetPendingEventCount(), 4);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 65534);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 65535);

    // Sequence ID 0 must be skipped on 16-bit wraparound
    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 1);

    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.sequenceId, 2);

    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);
}

TEST("SoulLink: Incoming command ring buffer deferred in battle and executed in overworld")
{
    struct SoulLinkCommand cmd1 = {0};
    struct SoulLinkCommand cmd2 = {0};

    SoulLink_Init();
    SoulLink_NewGame();

    cmd1.sequenceId = 101;
    cmd1.cmdType = SOULLINK_CMD_LOCK_ROUTE;
    cmd1.targetMapSec = MAPSEC_ROUTE_105;

    cmd2.sequenceId = 102;
    cmd2.cmdType = SOULLINK_CMD_UNLOCK_GYM;
    cmd2.paramValue = 1;
    cmd2.targetMapSec = 1;

    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd1), TRUE);
    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd2), TRUE);
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 2);

    // Simulate in-battle state: execution must be deferred
    SoulLink_TestSetSafeOverworldOverride(TRUE, FALSE);
    SoulLink_ProcessOverworldCommands();

    // Commands must remain untouched in buffer
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 2);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_105], ROUTE_STATE_OPEN);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 0);

    // Return to overworld: execution proceeds safely
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 0);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_105], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, cmd2.sequenceId);

    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);
}

TEST("SoulLink: Malformed and unknown commands are safely ignored without memory corruption")
{
    struct SoulLinkCommand malformed = {0};

    SoulLink_Init();
    SoulLink_NewGame();

    malformed.sequenceId = 201;
    malformed.cmdType = 99; // Unknown op
    malformed.targetMapSec = 250; // Out-of-bounds mapSec

    EXPECT_EQ(SoulLink_ReceiveCommand(&malformed), TRUE);
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 1);

    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    SoulLink_ProcessOverworldCommands();
    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);

    // Command queue must be safely drained without crash or corrupted route state
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 0);
    // Malformed commands are rejected, so lastExecutedCmdSequenceId must NOT be advanced to 201
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 0);
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_REJECTED);
}

TEST("SoulLink: Offline safety and missing bridge behavior returns ERR_NO_BRIDGE immediately")
{
    SoulLink_Init();
    EXPECT_EQ(SoulLink_GetTransportState(), SOULLINK_TRANSPORT_OFFLINE);

    // Create lobby request
    SoulLink_LobbyRequestCreate(4);
    EXPECT_EQ(gSoulLink.lobbyRequest.opCode, SOULLINK_LOBBY_OP_CREATE);
    EXPECT_EQ(gSoulLink.lobbyRequest.maxPlayers, 4);
    EXPECT_EQ(gSoulLink.lobbyResponse.ackRequestId, gSoulLink.lobbyRequest.requestId);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_ERR_NO_BRIDGE);

    // Join lobby request
    SoulLink_LobbyRequestJoin((const u8 *)"ROOM123");
    EXPECT_EQ(gSoulLink.lobbyRequest.opCode, SOULLINK_LOBBY_OP_JOIN);
    EXPECT_EQ(gSoulLink.lobbyResponse.ackRequestId, gSoulLink.lobbyRequest.requestId);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_ERR_NO_BRIDGE);

    // Ready lobby request
    SoulLink_LobbyRequestReady(TRUE);
    EXPECT_EQ(gSoulLink.lobbyRequest.opCode, SOULLINK_LOBBY_OP_READY);
    EXPECT_EQ(gSoulLink.lobbyResponse.ackRequestId, gSoulLink.lobbyRequest.requestId);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_ERR_NO_BRIDGE);

    // Start run request
    SoulLink_LobbyRequestStartRun();
    EXPECT_EQ(gSoulLink.lobbyRequest.opCode, SOULLINK_LOBBY_OP_START_RUN);
    EXPECT_EQ(gSoulLink.lobbyResponse.ackRequestId, gSoulLink.lobbyRequest.requestId);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_ERR_NO_BRIDGE);

    // When bridge connects, state transitions and requests become PENDING
    SoulLink_SetTransportState(SOULLINK_TRANSPORT_CONNECTED);
    EXPECT_EQ(SoulLink_GetTransportState(), SOULLINK_TRANSPORT_CONNECTED);

    // Clear request and response to test pending transition
    memset(&gSoulLink.lobbyRequest, 0, sizeof(gSoulLink.lobbyRequest));
    memset(&gSoulLink.lobbyResponse, 0, sizeof(gSoulLink.lobbyResponse));
    SoulLink_LobbyRequestCreate(2);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_PENDING);
}

TEST("SoulLink: Critical event overflow flags reconciliation and preserves local Nuzlocke state")
{
    u32 i;
    u8 metLoc = MAPSEC_ROUTE_101;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Fill queue to capacity (16 items)
    for (i = 0; i < SOULLINK_EVENT_QUEUE_SIZE; i++)
    {
        SoulLink_PushEvent(SOULLINK_EVENT_MAP_CHANGE, (u8)i, 0, 0, 0, 0);
    }
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    EXPECT_EQ(gSoulLink.eventTail, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 0);

    // Setup living mon on route 101
    gSoulLink.routeState[metLoc] = ROUTE_STATE_ALIVE;
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);

    // Mon faints in battle: RecordFaintEvent emits critical FAINTED event
    SoulLink_RecordFaintEvent(0);

    // Queue was full:
    // 1. eventTail was NOT moved by ROM (single-writer ownership preserved)
    EXPECT_EQ(gSoulLink.eventTail, 0);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    // 2. Critical overflow is tracked
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 1);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_CRITICAL_OVERFLOW);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);
    // 3. Local Nuzlocke death is NOT lost in game state
    EXPECT_EQ(gSoulLink.routeState[metLoc], ROUTE_STATE_DEAD_FORFEITED);

    SoulLink_ClearFaintEvents();
}

TEST("SoulLink: Sequence acknowledgments safely ignore zero, unknown, stale, future, and duplicate sequence IDs")
{
    SoulLink_Init();

    // Push 5 events (seq 1..5)
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 1, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 2, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 3, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 4, 0, 0, 0);
    SoulLink_EmitEvent(SOULLINK_EVENT_MAP_CHANGE, 5, 0, 0, 0);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 5);
    EXPECT_EQ(gSoulLink.eventTail, 0);

    // Ack with 0: ignored
    SoulLink_AcknowledgeEvents(0);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 5);
    EXPECT_EQ(gSoulLink.eventTail, 0);

    // Ack with unknown/future ID 999: ignored
    SoulLink_AcknowledgeEvents(999);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 5);
    EXPECT_EQ(gSoulLink.eventTail, 0);

    // Valid ack up to seq 3: advances tail by 3
    SoulLink_AcknowledgeEvents(3);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 2);
    EXPECT_EQ(gSoulLink.eventTail, 3);
    EXPECT_EQ(gSoulLink.lastAckedEventSequenceId, 3);

    // Stale ack (seq 2, already acknowledged): ignored
    SoulLink_AcknowledgeEvents(2);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 2);
    EXPECT_EQ(gSoulLink.eventTail, 3);

    // Duplicate ack (seq 3): ignored
    SoulLink_AcknowledgeEvents(3);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 2);
    EXPECT_EQ(gSoulLink.eventTail, 3);

    // Future jump (seq 10): ignored
    SoulLink_AcknowledgeEvents(10);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 2);
    EXPECT_EQ(gSoulLink.eventTail, 3);

    // Valid ack for seq 5: clears remaining events
    SoulLink_AcknowledgeEvents(5);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);
    EXPECT_EQ(gSoulLink.eventTail, 5);
    EXPECT_EQ(gSoulLink.lastAckedEventSequenceId, 5);
}

TEST("SoulLink: Incoming commands enforce duplicate/replay protection, execution status, and idempotent gym progression")
{
    struct SoulLinkCommand cmd = {0};

    SoulLink_Init();
    SoulLink_NewGame();
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);

    // 1. First command: UNLOCK_GYM stage 2 with sequence 10
    cmd.sequenceId = 10;
    cmd.cmdType = SOULLINK_CMD_UNLOCK_GYM;
    cmd.paramValue = 2;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.unlockedGymStage, 2);
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 10);
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);

    // 2. Replay duplicate command with sequence 10: must be marked DUPLICATE and not executed
    cmd.sequenceId = 10;
    cmd.paramValue = 3; // Even if conflicting param
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.unlockedGymStage, 2); // Unchanged!
    EXPECT_EQ(gSoulLink.cmdQueue[1].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    // 3. Stale sequence command (sequence 5 <= 10): marked DUPLICATE
    cmd.sequenceId = 5;
    cmd.paramValue = 4;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.unlockedGymStage, 2);
    EXPECT_EQ(gSoulLink.cmdQueue[2].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    // 4. Idempotent progression: New command seq 11 requesting lower/equal stage (stage 1)
    cmd.sequenceId = 11;
    cmd.paramValue = 1;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.unlockedGymStage, 2); // Cannot regress
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 11);
    EXPECT_EQ(gSoulLink.cmdQueue[3].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);

    // 5. TRIGGER_WIPE command is marked UNIMPLEMENTED
    cmd.sequenceId = 12;
    cmd.cmdType = SOULLINK_CMD_TRIGGER_WIPE;
    cmd.paramValue = 0;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();

    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 11); // Not advanced on UNIMPLEMENTED
    EXPECT_EQ(gSoulLink.cmdQueue[4].executionStatus, SOULLINK_CMD_STATUS_UNIMPLEMENTED);

    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);
}

TEST("SoulLink: Lobby requests reject concurrent pending requests and validate room code bounds")
{
    SoulLink_Init();
    SoulLink_SetTransportState(SOULLINK_TRANSPORT_CONNECTED);

    // First request initiates cleanly
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestCreate(4), TRUE);
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), TRUE);
    EXPECT_EQ(gSoulLink.lobbyResponse.responseCode, SOULLINK_RESP_PENDING);

    // Concurrent second request while pending must be REJECTED
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"ABCD12"), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestReady(TRUE), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestStartRun(), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestLeave(), FALSE);

    // Simulate coordinator response acknowledgment
    gSoulLink.lobbyResponse.ackRequestId = gSoulLink.lobbyRequest.requestId;
    gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_OK;
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), FALSE);

    // Room code validation: invalid characters rejected
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"BAD@$$"), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"SP ACE"), FALSE);
    EXPECT_EQ(SoulLink_LobbyRequestJoin(NULL), FALSE);

    // Room codes that cannot fit in 8 bytes without truncation are strictly rejected
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"12345678"), FALSE); // 8 chars + null needs 9 bytes
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"TOOLONGCODE"), FALSE);

    // Valid alphanumeric room code (up to 7 chars + null) accepted
    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"1234567"), TRUE); // Exactly 7 chars fits in 8 bytes
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), TRUE);

    // Reset pending status
    gSoulLink.lobbyResponse.ackRequestId = gSoulLink.lobbyRequest.requestId;
    gSoulLink.lobbyResponse.responseCode = SOULLINK_RESP_OK;
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), FALSE);

    EXPECT_EQ(SoulLink_LobbyRequestJoin((const u8 *)"ROOM42"), TRUE);
    EXPECT_EQ(SoulLink_IsLobbyRequestPending(), TRUE);
}

TEST("SoulLink: Safe overworld state verification checks callback, fade, battle, script, and lock status")
{
    MainCallback savedCallback = gMain.callback2;
    SoulLink_Init();
    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);

    ScriptContext_Init();
    UnlockPlayerFieldControls();
    gBattleTypeFlags = 0;
    gMain.callback2 = CB2_Overworld;
    gPaletteFade.active = 0;

    // Normal overworld state: safe
    EXPECT_EQ(SoulLink_IsSafeOverworldState(), TRUE);

    // Retained trainer battle flags post-battle do NOT block safe overworld state
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    EXPECT_EQ(SoulLink_IsSafeOverworldState(), TRUE);
    gBattleTypeFlags = 0;

    // Active battle or non-overworld callback (e.g. BattleMainCB2, party menu, PC, bag): unsafe
    gMain.callback2 = NULL;
    EXPECT_EQ(SoulLink_IsSafeOverworldState(), FALSE);
    gMain.callback2 = CB2_Overworld;

    // Screen fading / map transition: unsafe
    gPaletteFade.active = 1;
    EXPECT_EQ(SoulLink_IsSafeOverworldState(), FALSE);
    gPaletteFade.active = 0;

    EXPECT_EQ(SoulLink_IsSafeOverworldState(), TRUE);

    // Restore TestRunner callback so test runner loop continues safely
    gMain.callback2 = savedCallback;
}

TEST("SoulLink: Overworld commands execute safely after trainer battle with retained gBattleTypeFlags")
{
    MainCallback savedCallback = gMain.callback2;
    struct SoulLinkCommand cmd = {0};

    SoulLink_Init();
    SoulLink_NewGame();
    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);

    cmd.sequenceId = 42;
    cmd.cmdType = SOULLINK_CMD_LOCK_ROUTE;
    cmd.targetMapSec = MAPSEC_ROUTE_106;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;

    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd), TRUE);
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 1);

    // 1. In battle: gMain.callback2 is battle callback (not CB2_Overworld), battle flags set
    gMain.callback2 = NULL;
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    SoulLink_ProcessOverworldCommands();

    // Execution must be deferred while in battle
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 1);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_106], ROUTE_STATE_OPEN);

    // 2. Battle finishes and returns to overworld:
    // Game engine sets CB2_Overworld, fades in, and restores controls.
    // Crucially: gBattleTypeFlags REMAINS BATTLE_TYPE_TRAINER!
    gMain.callback2 = CB2_Overworld;
    gPaletteFade.active = 0;
    ScriptContext_Init();
    UnlockPlayerFieldControls();

    EXPECT_EQ(SoulLink_IsSafeOverworldState(), TRUE);
    SoulLink_ProcessOverworldCommands();

    // Queued command must execute successfully despite retained battle flags!
    EXPECT_EQ(SoulLink_GetPendingCommandCount(), 0);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_106], ROUTE_STATE_DEAD_FORFEITED);
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 42);

    gBattleTypeFlags = 0;
    gMain.callback2 = savedCallback;
}

TEST("SoulLink: Command sequence wraparound 65534 to 65535 to 1 to 2, stale rejection, and session reset")
{
    struct SoulLinkCommand cmd = {0};

    SoulLink_Init();
    SoulLink_NewGame();
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);

    // Start near 16-bit sequence boundary
    gSoulLink.lastExecutedCmdSequenceId = 65533;

    // 1. Transition 65534: Fresh forward command
    cmd.sequenceId = 65534;
    cmd.cmdType = SOULLINK_CMD_UNLOCK_GYM;
    cmd.paramValue = 1;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 65534);
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);

    // 2. Transition 65535: Boundary value
    cmd.sequenceId = 65535;
    cmd.paramValue = 2;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 65535);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 2);

    // 3. Wraparound transition across 65535 -> 1 (skipping sequence 0)
    cmd.sequenceId = 1;
    cmd.paramValue = 3;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 1);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 3);

    // 4. Transition 1 -> 2
    cmd.sequenceId = 2;
    cmd.paramValue = 4;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 2);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 4);

    // 5. Duplicate of current sequence (seq 2): must be rejected as DUPLICATE
    cmd.sequenceId = 2;
    cmd.paramValue = 5;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 2); // Unchanged!
    EXPECT_EQ(gSoulLink.unlockedGymStage, 4);          // Unchanged!
    EXPECT_EQ(gSoulLink.cmdQueue[4].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    // 6. Stale commands across wraparound boundary (seq 1, 65535, 65534)
    cmd.sequenceId = 1;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 2);
    EXPECT_EQ(gSoulLink.cmdQueue[5].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    cmd.sequenceId = 65535;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 2);
    EXPECT_EQ(gSoulLink.cmdQueue[6].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    cmd.sequenceId = 65534;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 2);
    EXPECT_EQ(gSoulLink.cmdQueue[7].executionStatus, SOULLINK_CMD_STATUS_DUPLICATE);

    // 7. Session reset / reboot: SoulLink_Init generates new sessionEpoch and resets lastExecuted
    SoulLink_Init();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 0);
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);

    // In new session epoch, sequence 1 is accepted cleanly
    cmd.sequenceId = 1;
    cmd.cmdType = SOULLINK_CMD_UNLOCK_GYM;
    cmd.paramValue = 5;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;
    SoulLink_ReceiveCommand(&cmd);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.lastExecutedCmdSequenceId, 1);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 5);

    SoulLink_TestSetSafeOverworldOverride(FALSE, FALSE);
}

TEST("SoulLink: Critical event classification includes CAUGHT and BADGE with queue overflow reconciliation")
{
    struct SoulLinkEvent evt;
    u32 i;

    SoulLink_Init();
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 0);

    // 1. Verify SoulLink_EmitEvent marks CAUGHT as critical
    SoulLink_EmitEvent(SOULLINK_EVENT_CAUGHT, MAPSEC_ROUTE_101, SPECIES_POOCHYENA, TYPE_DARK, 0);
    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_CAUGHT);
    EXPECT(evt.flags & SOULLINK_EVT_FLAG_CRITICAL);

    // 2. Verify SoulLink_EmitEvent marks BADGE as critical
    SoulLink_EmitEvent(SOULLINK_EVENT_BADGE, MAPSEC_RUSTBORO_CITY, 0, 0, 1);
    EXPECT_EQ(SoulLink_PopEvent(&evt), TRUE);
    EXPECT_EQ(evt.eventType, SOULLINK_EVENT_BADGE);
    EXPECT(evt.flags & SOULLINK_EVT_FLAG_CRITICAL);

    // 3. Re-init queue and fill to capacity (16 non-critical events)
    SoulLink_Init();
    for (i = 0; i < SOULLINK_EVENT_QUEUE_SIZE; i++)
    {
        SoulLink_PushEvent(SOULLINK_EVENT_MAP_CHANGE, (u8)i, 0, 0, 0, 0);
    }
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 0);

    // 4. Verify each critical event type triggers critical overflow and sets reconciliation-required flag:
    // a. FAINTED
    SoulLink_EmitEvent(SOULLINK_EVENT_FAINTED, MAPSEC_ROUTE_102, SPECIES_TREECKO, TYPE_GRASS, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 1);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // b. MISSED
    SoulLink_EmitEvent(SOULLINK_EVENT_MISSED, MAPSEC_ROUTE_103, 0, 0, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 2);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // c. WIPE
    SoulLink_EmitEvent(SOULLINK_EVENT_WIPE, MAPSEC_ROUTE_104, 0, 0, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 3);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // d. CAUGHT
    SoulLink_EmitEvent(SOULLINK_EVENT_CAUGHT, MAPSEC_ROUTE_105, SPECIES_MAGIKARP, TYPE_WATER, 0);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 4);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // e. BADGE
    SoulLink_EmitEvent(SOULLINK_EVENT_BADGE, MAPSEC_DEWFORD_TOWN, 0, 0, 2);
    EXPECT_EQ(SoulLink_GetCriticalOverflowCount(), 5);
    EXPECT(gSoulLink.mailboxFlags & SOULLINK_FLAG_RECONCILIATION_REQUIRED);

    // Verify ROM CPU never advanced eventTail on overflow
    EXPECT_EQ(gSoulLink.eventTail, 0);
    EXPECT_EQ(SoulLink_GetPendingEventCount(), 16);
}

TEST("SoulLink: Session epoch generates distinct non-zero session identity across boots")
{
    SoulLink_Init();
    u32 epoch1 = gSoulLink.sessionEpoch;
    EXPECT(epoch1 != 0);

    // Second boot / reset produces a different non-zero epoch
    SoulLink_Init();
    u32 epoch2 = gSoulLink.sessionEpoch;
    EXPECT(epoch2 != 0);
    EXPECT(epoch1 != epoch2);

    // Test helper to set epoch
    SoulLink_TestSetSessionEpoch(0x12345678);
    EXPECT_EQ(gSoulLink.sessionEpoch, 0x12345678);
}

TEST("SoulLink: KILL_MON transfers live party member to Graveyard box and compacts party")
{
    u32 i;
    u8 metLoc101 = MAPSEC_ROUTE_101;
    u8 metLoc102 = MAPSEC_ROUTE_102;
    struct SoulLinkCommand cmd;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Clear party
    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    // Create 2 Pokémon: Treecko on Route 101, Zigzagoon on Route 102
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc101);

    CreateMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_ZIGZAGOON, 5, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][1]);
    SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_MET_LOCATION, &metLoc102);

    gSoulLink.routeState[MAPSEC_ROUTE_101] = ROUTE_STATE_ALIVE;
    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_ALIVE;
    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 2);
    EXPECT_EQ(CountMonsInBox(SOULLINK_GRAVEYARD_BOX), 0);

    // Queue KILL_MON command for Route 102
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    memset(&cmd, 0, sizeof(cmd));
    cmd.sequenceId = 1;
    cmd.cmdType = SOULLINK_CMD_KILL_MON;
    cmd.targetMapSec = MAPSEC_ROUTE_102;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;

    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd), TRUE);
    SoulLink_ProcessOverworldCommands();

    // Verify command executed successfully
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);

    // Party should now have 1 living Pokémon (Treecko) and be compacted
    EXPECT_EQ(SoulLink_GetLivingPokemonCount(), 1);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES), SPECIES_TREECKO);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_SPECIES), SPECIES_NONE);

    // Graveyard box (Box 14) should now contain the dead Zigzagoon
    EXPECT_EQ(CountMonsInBox(SOULLINK_GRAVEYARD_BOX), 1);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SPECIES), SPECIES_ZIGZAGOON);

    // Idempotent re-run: replaying command does not duplicate or crash
    memset(&cmd, 0, sizeof(cmd));
    cmd.sequenceId = 2;
    cmd.cmdType = SOULLINK_CMD_KILL_MON;
    cmd.targetMapSec = MAPSEC_ROUTE_102;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;

    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd), TRUE);
    SoulLink_ProcessOverworldCommands();
    EXPECT_EQ(gSoulLink.cmdQueue[1].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
    EXPECT_EQ(CountMonsInBox(SOULLINK_GRAVEYARD_BOX), 1); // Still exactly 1
}

TEST("SoulLink: KILL_MON transfers PC-stored Pokemon to Graveyard box and zeroes original box slot")
{
    u32 i;
    u8 metLoc101 = MAPSEC_ROUTE_101;
    u8 metLoc103 = MAPSEC_ROUTE_103;
    struct Pokemon tempMon;
    struct SoulLinkCommand cmd;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Clear party
    for (i = 0; i < PARTY_SIZE; i++)
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);

    // Active party mon on Route 101
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc101);

    // Boxed mon on Route 103 in Box 2, Slot 5
    CreateMon(&tempMon, SPECIES_WINGULL, 7, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&tempMon);
    SetMonData(&tempMon, MON_DATA_MET_LOCATION, &metLoc103);
    SetBoxMonAt(2, 5, &tempMon.box);

    EXPECT_EQ(GetBoxMonDataAt(2, 5, MON_DATA_SPECIES), SPECIES_WINGULL);
    EXPECT_EQ(CountMonsInBox(SOULLINK_GRAVEYARD_BOX), 0);
    gSoulLink.routeState[MAPSEC_ROUTE_103] = ROUTE_STATE_ALIVE;

    // Queue KILL_MON command for Route 103
    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    memset(&cmd, 0, sizeof(cmd));
    cmd.sequenceId = 1;
    cmd.cmdType = SOULLINK_CMD_KILL_MON;
    cmd.targetMapSec = MAPSEC_ROUTE_103;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;

    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd), TRUE);
    SoulLink_ProcessOverworldCommands();

    // Verify command succeeded
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_103], ROUTE_STATE_DEAD_FORFEITED);

    // Box 2, Slot 5 should now be empty (zeroed)
    EXPECT_EQ(GetBoxMonDataAt(2, 5, MON_DATA_SPECIES), SPECIES_NONE);

    // Graveyard box should contain the Wingull
    EXPECT_EQ(CountMonsInBox(SOULLINK_GRAVEYARD_BOX), 1);
    EXPECT_EQ(GetBoxMonDataAt(SOULLINK_GRAVEYARD_BOX, 0, MON_DATA_SPECIES), SPECIES_WINGULL);
}

TEST("SoulLink: Gym progression unlocks locally in offline mode, but strictly requires coordinator command in multiplayer")
{
    SoulLink_Init();
    SoulLink_NewGame();
    EXPECT_EQ(gSoulLink.unlockedGymStage, 0);

    // Case 1: Offline mode (multiplayer inactive) -> local badge unlocks gym stage
    gSoulLink.transportState = SOULLINK_TRANSPORT_OFFLINE;
    gSoulLink.lobbyResponse.runState = SOULLINK_RUN_STATE_LOBBY;
    EXPECT_EQ(SoulLink_IsMultiplayerActive(), FALSE);

    // Simulate earning badge 1
    FlagSet(FLAG_BADGE01_GET);
    SoulLink_OnBadgeEarned();
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);

    // Case 2: Multiplayer active -> local badge 2 does NOT advance unlockedGymStage!
    gSoulLink.transportState = SOULLINK_TRANSPORT_CONNECTED;
    EXPECT_EQ(SoulLink_IsMultiplayerActive(), TRUE);

    // Simulate earning badge 2 locally
    FlagSet(FLAG_BADGE02_GET);
    SoulLink_OnBadgeEarned();
    // Must REMAIN at stage 1 until coordinator issues UNLOCK_GYM!
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);

    // Coordinator sends UNLOCK_GYM stage 2 command
    struct SoulLinkCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.sequenceId = 1;
    cmd.cmdType = SOULLINK_CMD_UNLOCK_GYM;
    cmd.paramValue = 2;
    cmd.executionStatus = SOULLINK_CMD_STATUS_PENDING;

    SoulLink_TestSetSafeOverworldOverride(TRUE, TRUE);
    EXPECT_EQ(SoulLink_ReceiveCommand(&cmd), TRUE);
    SoulLink_ProcessOverworldCommands();

    // Now unlockedGymStage is advanced to 2
    EXPECT_EQ(gSoulLink.cmdQueue[0].executionStatus, SOULLINK_CMD_STATUS_SUCCESS);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 2);
}
