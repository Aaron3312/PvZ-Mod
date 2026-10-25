#include <algorithm>
#include "Mods.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/TitleScreen.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/SeedChooserScreen.h"
#include "Lawn/Widget/ChallengeScreen.h"
#include "ModContent.h"
#include "HackMenu.h"
#include <format>
#include "Lawn/Projectile.h"
#include "PvzpLib/PvzpDebug.h"
#include <SDL.h>

namespace
{
	ModSettings gSettings;
	bool gAppliedEasyPlanting = false;

	constexpr int MAX_SUN = 9990;
	constexpr int MAX_COINS = 99999;	// the game stores money / 10 and clamps it here ($999,990)
	constexpr int ONE_HIT_DAMAGE = 1000000;

	struct IntField
	{
		const char*	mKey;
		int*		mValue;
	};

	struct BoolField
	{
		const char*	mKey;
		bool*		mValue;
	};

	const IntField gIntFields[] = {
		{ "ModCostPercent", &gSettings.mCostPercent },
		{ "ModDamagePercent", &gSettings.mDamagePercent },
		{ "ModZombieHealthPercent", &gSettings.mZombieHealthPercent },
		{ "ModSpeed", &gSettings.mSpeed },
	};

	const BoolField gBoolFields[] = {
		{ "ModInfiniteSun", &gSettings.mInfiniteSun },
		{ "ModNoRecharge", &gSettings.mNoRecharge },
		{ "ModEasyPlanting", &gSettings.mEasyPlanting },
		{ "ModInvinciblePlants", &gSettings.mInvinciblePlants },
		{ "ModFreezeZombies", &gSettings.mFreezeZombies },
		{ "ModOneHitKill", &gSettings.mOneHitKill },
		{ "ModInfiniteCoins", &gSettings.mInfiniteCoins },
	};

	// PVZ_MOD_TEST=1 drives the game without a mouse so new content can be checked on screen:
	// skip the title, start the adventure, let the seed chooser pick, then place the mod plants
	// and spawn the mod zombies once. Run it with -savedir pointing at a copy of the saves.
	// PVZ_MOD_TEST_SHOTS=<folder>: the test saves its own frames there, so nobody's screen is captured.
	void TestScreenshot(const char* theName)
	{
		const char* aFolder = SDL_getenv("PVZ_MOD_TEST_SHOTS");
		if (aFolder)
			HackMenu::RequestScreenshot(std::format("{}/{}.png", aFolder, theName).c_str());
	}

	// PVZ_MOD_TEST=modes opens the Mods minigame page; =mode1..mode5 also plays that mode.
	int TestModeNumber()
	{
		static const int sModeNumber = [] {
			const char* aTest = SDL_getenv("PVZ_MOD_TEST");
			if (SDL_strcmp(aTest, "modes") == 0)
				return 0;
			if (SDL_strncmp(aTest, "mode", 4) == 0)
				return SDL_atoi(aTest + 4);
			return -1;
		}();
		return sModeNumber;
	}

	void UpdateModTest(LawnApp* theApp)
	{
		static int sStep = 0;
		static int sTicks = 0;
		static int sTotalTicks = 0;
		sTicks++;
		sTotalTicks++;
		// PVZ_MOD_TEST_DEBUG: a frame every 5 s with the step, to see where a test gets stuck
		static const bool sDebugShots = SDL_getenv("PVZ_MOD_TEST_DEBUG") != nullptr;
		if (sDebugShots && sTotalTicks % 500 == 0)
			TestScreenshot(std::format("dbg-{:05}-step{}", sTotalTicks, sStep).c_str());
		switch (sStep)
		{
		case 0:
			if (theApp->mTitleScreen && theApp->mTitleScreen->mLoadingThreadComplete)
			{
				theApp->LoadingCompleted();
				sStep = 1;
				sTicks = 0;
			}
			break;
		case 1:
			if (TestModeNumber() >= 0 && theApp->mGameSelector && theApp->mGameScene == GameScenes::SCENE_MENU && sTicks > 100)
			{
				theApp->mGameSelector->ButtonDepress(101);	// GameSelector_Minigame (private)
				sStep = 10;
				sTicks = 0;
			}
			else if (theApp->mGameSelector && theApp->mGameScene == GameScenes::SCENE_MENU && sTicks > 100)
			{
				// A click on Adventure, so the selector runs its own transition (PreNewGame here would
				// delete the selector in the middle of its Update).
				theApp->mGameSelector->ButtonDepress(100);	// GameSelector::GameSelector_Adventure (private)
				sStep = 2;
				sTicks = 0;
			}
			break;
		case 2:
			if (theApp->mGameScene == GameScenes::SCENE_PLAYING)
			{
				sStep = 3;
				sTicks = 0;
			}
			else if (theApp->mSeedChooserScreen && sTicks > 500)
			{
				// PVZ_MOD_TEST=chooser stays on the seed chooser with the Mods page open.
				static const bool sStayInChooser = SDL_strcmp(SDL_getenv("PVZ_MOD_TEST"), "chooser") == 0;
				if (sStayInChooser)
				{
					if (!theApp->mSeedChooserScreen->mShowModPage)
						theApp->mSeedChooserScreen->ButtonDepress(107);	// SeedChooserScreen_ModPage (private)
					else
						TestScreenshot("chooser-mods");
				}
				else
				{
					// Only once: calling it again while the chooser slides away restarts the "Ready, Set, Plant" intro.
					static bool sPicked = false;
					if (!sPicked)
						theApp->mSeedChooserScreen->PickRandomSeeds();
					sPicked = true;
				}
				sTicks = 0;
			}
			break;
		case 10:
			if (theApp->mChallengeScreen && sTicks > 60)
			{
				theApp->mChallengeScreen->ButtonDepress(300 + ChallengePage::CHALLENGE_PAGE_MODS);	// ChallengeScreen_Page + page
				sStep = 11;
				sTicks = 0;
			}
			break;
		case 11:
			if (sTicks == 60)
				TestScreenshot("challenge-mods");
			if (sTicks > 90 && TestModeNumber() > 0)
			{
				int aMode = GameMode::GAMEMODE_MOD_INVASION + TestModeNumber() - 1;
				theApp->mChallengeScreen->ButtonDepress(200 + aMode - 1);	// ChallengeScreen_Mode + challenge index
				sStep = 2;
				sTicks = 0;
			}
			break;
		case 3:
			if (TestModeNumber() > 0 && theApp->mBoard && theApp->mGameScene == GameScenes::SCENE_PLAYING && sTicks > 250)
			{
				// In a mod mode: two of each zombie the mode allows, no plants, to see the mode itself.
				Board* aBoard = theApp->mBoard;
				for (int aType = 0; aType < ZombieType::NUM_ZOMBIE_TYPES; aType++)
				{
					if (!aBoard->mZombieAllowed[aType])
						continue;
					for (int i = 0; i < 2; i++)
						aBoard->AddZombie(static_cast<ZombieType>(aType), Zombie::ZOMBIE_WAVE_DEBUG);
					PvzpLogLn("ModTest: mode {} allows zombie type {}", static_cast<int>(theApp->mGameMode), aType);
				}
				sStep = 4;
				sTicks = 0;
			}
			else if (theApp->mBoard && theApp->mGameScene == GameScenes::SCENE_PLAYING && sTicks > 250)
			{
				Board* aBoard = theApp->mBoard;
				for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
				{
					for (SeedType aSeedType = ModContent::FIRST_MOD_SEED; aSeedType < SeedType::NUM_SEED_TYPES; aSeedType = static_cast<SeedType>(aSeedType + 1))
					{
						int aCol = 1 + (aSeedType - ModContent::FIRST_MOD_SEED);
						if (aBoard->CanPlantAt(aCol, aRow, aSeedType) == PlantingReason::PLANTING_OK)
							aBoard->AddPlant(aCol, aRow, aSeedType);
					}
					if (aBoard->CanPlantAt(0, aRow, SeedType::SEED_PEASHOOTER) == PlantingReason::PLANTING_OK)
						aBoard->AddPlant(0, aRow, SeedType::SEED_PEASHOOTER);	// an original next to it, for comparison
				}
				for (ZombieType aZombieType = ModContent::FIRST_MOD_ZOMBIE; aZombieType < ZombieType::NUM_ZOMBIE_TYPES; aZombieType = static_cast<ZombieType>(aZombieType + 1))
				{
					for (int i = 0; i < 3; i++)
					{
						Zombie* aZombie = aBoard->AddZombie(aZombieType, Zombie::ZOMBIE_WAVE_DEBUG);
						PvzpLogLn("ModTest: spawned zombie type {} -> {}", static_cast<int>(aZombieType), aZombie ? aZombie->mRow : -99);
					}
				}
				aBoard->AddZombie(ZombieType::ZOMBIE_NORMAL, Zombie::ZOMBIE_WAVE_DEBUG);
				sStep = 4;
				sTicks = 0;
			}
			break;
		case 4:
			// Every 2 s: each zombie and the projectile mix, so the result can be read from the log.
			if (sTicks == 600 || sTicks == 900)
				TestScreenshot(TestModeNumber() > 0 ? std::format("mode{}-{}", TestModeNumber(), sTicks).c_str() : std::format("board-{}", sTicks).c_str());
			// PVZ_MOD_TEST_SAVELOAD: at 10 s, save, throw the board away and load it again (what "Continue" does),
			// to check that the mod fields survive a saved game. The file goes next to the screenshots.
			static const bool sSaveLoad = SDL_getenv("PVZ_MOD_TEST_SAVELOAD") != nullptr && SDL_getenv("PVZ_MOD_TEST_SHOTS") != nullptr;
			if (sSaveLoad && sTicks == 1000 && theApp->mBoard)
			{
				auto aSummary = [](Board* theBoard)
				{
					int aPoisoned = 0, aZombieTimers = 0, aDeathDone = 0, aPlantTimers = 0, aPoisonPeas = 0;
					for (Zombie* aZombie : theBoard->mZombies)
					{
						if (aZombie->mDead)
							continue;
						aPoisoned += aZombie->mModPoisonCounter > 0;
						aZombieTimers += aZombie->mModTimer;
						aDeathDone += aZombie->mModDeathDone;
					}
					for (Plant* aPlant : theBoard->mPlants)
						if (!aPlant->mDead)
							aPlantTimers += aPlant->mModTimer;
					for (Projectile* aProjectile : theBoard->mProjectiles)
						if (!aProjectile->mDead)
							aPoisonPeas += aProjectile->mModPoison;
					return std::format("poisoned {} zombie timers {} death done {} plant timers {} poison peas {}", aPoisoned, aZombieTimers, aDeathDone, aPlantTimers, aPoisonPeas);
				};
				std::string aPath = std::format("{}/modtest-save.v4", SDL_getenv("PVZ_MOD_TEST_SHOTS"));
				PvzpLogLn("ModTest: before save: {}", aSummary(theApp->mBoard));
				theApp->mBoard->SaveGame(aPath);
				theApp->MakeNewBoard();
				bool aLoaded = theApp->mBoard->LoadGame(aPath);
				PvzpLogLn("ModTest: loaded {}: {}", aLoaded, aSummary(theApp->mBoard));
			}
			if (theApp->mBoard && sTicks % 200 == 0 && sTicks <= 3000)
			{
				for (Zombie* aZombie : theApp->mBoard->mZombies)
				{
					if (!aZombie->mDead)
						PvzpLogLn("ModTest: t={} zombie type {} row {} x {:.0f} velX {:.2f} body {} helm {} chill {} frozen {} poison {} scale {:.1f}", sTicks, static_cast<int>(aZombie->mZombieType), aZombie->mRow, aZombie->mPosX, aZombie->mVelX, aZombie->mBodyHealth, aZombie->mHelmHealth, aZombie->mChilledCounter, aZombie->mIceTrapCounter, aZombie->mModPoisonCounter, aZombie->mScaleZombie);
				}
				int aCount[16] = {};
				for (Projectile* aProjectile : theApp->mBoard->mProjectiles)
				{
					if (!aProjectile->mDead && aProjectile->mProjectileType >= 0 && aProjectile->mProjectileType < 16)
						aCount[aProjectile->mProjectileType]++;
				}
				int aPlants = 0;
				int aHurtPlants = 0;
				for (Plant* aPlant : theApp->mBoard->mPlants)
				{
					if (aPlant->mDead)
						continue;
					aPlants++;
					if (aPlant->mPlantHealth < aPlant->mPlantMaxHealth)
						aHurtPlants++;
				}
				PvzpLogLn("ModTest: t={} projectiles pea {} fireball {} plants {} hurt {}", sTicks, aCount[ProjectileType::PROJECTILE_PEA], aCount[ProjectileType::PROJECTILE_FIREBALL], aPlants, aHurtPlants);
			}
			break;
		default:
			break;
		}
	}

	int ScalePercent(int theValue, int thePercent)
	{
		if (theValue <= 0 || thePercent == 100)
			return theValue;
		return std::max(1, static_cast<int>(static_cast<long long>(theValue) * thePercent / 100));
	}

	void ClampSettings()
	{
		gSettings.mCostPercent = std::clamp(gSettings.mCostPercent, 0, 100);
		gSettings.mDamagePercent = std::clamp(gSettings.mDamagePercent, 10, 2000);
		gSettings.mZombieHealthPercent = std::clamp(gSettings.mZombieHealthPercent, 10, 1000);
		gSettings.mSpeed = std::clamp(gSettings.mSpeed, 1, 10);
	}
}

ModSettings& Mods::Settings()
{
	return gSettings;
}

void Mods::Load(LawnApp* theApp)
{
	for (const IntField& aField : gIntFields)
		theApp->RegistryReadInteger(aField.mKey, aField.mValue);
	for (const BoolField& aField : gBoolFields)
		theApp->RegistryReadBoolean(aField.mKey, aField.mValue);
	ClampSettings();
}

void Mods::Save(LawnApp* theApp)
{
	ClampSettings();
	for (const IntField& aField : gIntFields)
		theApp->RegistryWriteInteger(aField.mKey, *aField.mValue);
	for (const BoolField& aField : gBoolFields)
		theApp->RegistryWriteBoolean(aField.mKey, *aField.mValue);
}

void Mods::OnUpdate(LawnApp* theApp)
{
	static const bool sModTest = SDL_getenv("PVZ_MOD_TEST") != nullptr;
	if (sModTest)
		UpdateModTest(theApp);

	// Only touch the cheat flag while the menu owns it, so the '8' debug key keeps working.
	if (gSettings.mEasyPlanting != gAppliedEasyPlanting)
	{
		theApp->mEasyPlantingCheat = gSettings.mEasyPlanting;
		gAppliedEasyPlanting = gSettings.mEasyPlanting;
	}

	if (gSettings.mInfiniteCoins && theApp->mPlayerInfo && theApp->mPlayerInfo->mCoins < MAX_COINS)
		theApp->mPlayerInfo->mCoins = MAX_COINS;

	Board* aBoard = theApp->mBoard;
	if (!aBoard)
		return;

	if (gSettings.mInfiniteSun && aBoard->mSunMoney < MAX_SUN)
		aBoard->mSunMoney = MAX_SUN;

	if (gSettings.mNoRecharge && aBoard->mSeedBank)
	{
		for (int i = 0; i < aBoard->mSeedBank->mNumPackets; i++)
		{
			SeedPacket& aPacket = aBoard->mSeedBank->mSeedPackets[i];
			if (aPacket.mRefreshing)
				aPacket.mRefreshCounter = aPacket.mRefreshTime;	// SeedPacket::Update finishes it on the next tick
		}
	}

	if (gSettings.mInvinciblePlants)
	{
		for (Plant* aPlant : aBoard->mPlants)
		{
			if (!aPlant->mDead)
				aPlant->mPlantHealth = aPlant->mPlantMaxHealth;
		}
	}

	if (gSettings.mFreezeZombies)
	{
		for (Zombie* aZombie : aBoard->mZombies)
		{
			if (!aZombie->mDead && !aZombie->IsDeadOrDying())
				aZombie->mIceTrapCounter = std::max(aZombie->mIceTrapCounter, 2);
		}
	}
}

int Mods::UpdatesPerFrame()
{
	return gSettings.mSpeed;
}

int Mods::ScaleCost(int theCost)
{
	return gSettings.mCostPercent >= 100 ? theCost : theCost * gSettings.mCostPercent / 100;
}

int Mods::ScaleDamage(int theDamage)
{
	if (gSettings.mOneHitKill && theDamage > 0)
		return ONE_HIT_DAMAGE;
	return ScalePercent(theDamage, gSettings.mDamagePercent);
}

void Mods::ScaleZombieHealth(Zombie* theZombie)
{
	const int aPercent = gSettings.mZombieHealthPercent;
	if (aPercent == 100)
		return;
	theZombie->mBodyHealth = ScalePercent(theZombie->mBodyHealth, aPercent);
	theZombie->mBodyMaxHealth = ScalePercent(theZombie->mBodyMaxHealth, aPercent);
	theZombie->mHelmHealth = ScalePercent(theZombie->mHelmHealth, aPercent);
	theZombie->mHelmMaxHealth = ScalePercent(theZombie->mHelmMaxHealth, aPercent);
	theZombie->mShieldHealth = ScalePercent(theZombie->mShieldHealth, aPercent);
	theZombie->mShieldMaxHealth = ScalePercent(theZombie->mShieldMaxHealth, aPercent);
	theZombie->mFlyingHealth = ScalePercent(theZombie->mFlyingHealth, aPercent);
	theZombie->mFlyingMaxHealth = ScalePercent(theZombie->mFlyingMaxHealth, aPercent);
}
