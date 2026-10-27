#include "global.h"
#include "battle.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "event_data.h"
#include "load_save.h"
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

TEST("SoulLink: Graveyard PC storage blocks multi-selection operations")
{
    SoulLink_Init();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Set storage to Graveyard box
    SetCurrentBox(SOULLINK_GRAVEYARD_BOX);
    EXPECT_EQ(StorageGetCurrentBox(), SOULLINK_GRAVEYARD_BOX);

    // MultiMove_CanPlaceSelection must return FALSE when in Graveyard box
    EXPECT_EQ(MultiMove_CanPlaceSelection(), FALSE);

    // Normal box 0 with no active multimove
    SetCurrentBox(0);
    EXPECT_EQ(StorageGetCurrentBox(), 0);
    EXPECT_EQ(MultiMove_CanPlaceSelection(), FALSE);
}
