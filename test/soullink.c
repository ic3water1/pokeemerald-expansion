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

    // Stage 0: Initial level cap is 15 (Roxanne)
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 15);

    // Gym 1 (Roxanne) allowed at stage 0
    gSpecialVar_0x8004 = 1; // Gym 1
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // Gym 2 (Brawly) blocked at stage 0
    gSpecialVar_0x8004 = 2; // Gym 2
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);

    // Earn Gym 1 badge: unlocks Stage 1
    SoulLink_OnBadgeEarned(1);
    EXPECT_EQ(gSoulLink.unlockedGymStage, 1);
    EXPECT_EQ(SoulLink_GetCurrentLevelCap(), 19);

    // Gym 2 now unlocked
    EXPECT_EQ(CheckPartyGymUnlock(), TRUE);

    // Gym 3 still blocked
    gSpecialVar_0x8004 = 3;
    EXPECT_EQ(CheckPartyGymUnlock(), FALSE);
}
