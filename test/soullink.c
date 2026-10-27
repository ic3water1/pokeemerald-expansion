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

TEST("SoulLink: Gym progression locks and hard level caps")
{
    SoulLink_Init();
    SoulLink_NewGame();

    // Stage 0: Initial level cap is 15 (Roxanne)
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 15);

    // Gym 1 (Roxanne) allowed at stage 0
    gSpecialVar_0x8004 = 1; // Gym 1
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // Gym 2 (Brawly) blocked at stage 0
    gSpecialVar_0x8004 = 2; // Gym 2
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);

    // Set badge 1 flag and call SoulLink_OnBadgeEarned special
    FlagSet(FLAG_BADGE01_GET);
    SoulLink_OnBadgeEarned();
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 19);

    // Gym 2 now unlocked
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // Gym 3 still blocked
    gSpecialVar_0x8004 = 3;
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);

    // Earn badges 2, 3, 4
    FlagSet(FLAG_BADGE02_GET);
    FlagSet(FLAG_BADGE03_GET);
    FlagSet(FLAG_BADGE04_GET);
    SoulLink_OnBadgeEarned();
    EXPECT_EQ(gSoulLink.unlockedGymStage, 4);
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 31);
}

TEST("SoulLink: Save and load persistence via SaveBlock3")
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

    // Save state to SaveBlock3
    SoulLink_SaveToSaveBlock3();

    // Simulate reboot: SoulLink_Init() must NOT wipe save data from SaveBlock3
    SoulLink_Init();

    // Clear EWRAM routeState to ensure load actually restores it
    gSoulLink.routeState[MAPSEC_ROUTE_101] = ROUTE_STATE_OPEN;
    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_OPEN;
    gSoulLink.unlockedGymStage = 0;

    // Restore from SaveBlock3
    SoulLink_LoadFromSaveBlock3();

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

TEST("SoulLink: Catch validation guard blocks catching on dead routes")
{
    SoulLink_Init();
    SoulLink_NewGame();

    gMapHeader.regionMapSectionId = MAPSEC_ROUTE_102;
    gSoulLink.routeState[MAPSEC_ROUTE_102] = ROUTE_STATE_DEAD_FORFEITED;

    // Attempt to register a catch on a dead route
    SoulLink_OnWildCatchSuccess(SPECIES_RALTS);

    // Route must remain DEAD_FORFEITED and not change to ALIVE
    EXPECT_EQ(gSoulLink.routeState[MAPSEC_ROUTE_102], ROUTE_STATE_DEAD_FORFEITED);
}

TEST("SoulLink: Encounter identity correctly attributes Route 104 North vs South")
{
    u8 metLoc = MAPSEC_ROUTE_104;
    u16 zeroHp = 0;

    SoulLink_Init();
    SoulLink_NewGame();
    gPokemonStoragePtr = &gPokemonStorage.block;
    ResetPokemonStorageSystem();

    // Set Route 104 North to ALIVE with Treecko
    gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH] = ROUTE_STATE_ALIVE;
    gSoulLink.routeSpecies[ENCOUNTER_ID_ROUTE_104_NORTH][0] = SPECIES_TREECKO;

    // Route 104 South remains OPEN
    gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_SOUTH] = ROUTE_STATE_OPEN;

    // Create Treecko with standard Route 104 met-location
    ZeroMonData(&gParties[B_TRAINER_PLAYER][0]);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 10, 0, OTID_STRUCT_PRESET(0));
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MET_LOCATION, &metLoc);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &zeroHp);

    SoulLink_ProcessFaintedMonsPostBattle();

    // Route 104 North must be marked dead
    EXPECT_EQ(gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_NORTH], ROUTE_STATE_DEAD_FORFEITED);

    // Route 104 South must remain OPEN and unaffected!
    EXPECT_EQ(gSoulLink.routeState[ENCOUNTER_ID_ROUTE_104_SOUTH], ROUTE_STATE_OPEN);
}
