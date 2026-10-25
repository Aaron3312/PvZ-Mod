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
#include "ModContent.h"
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
	void UpdateModTest(LawnApp* theApp)
	{
		static int sStep = 0;
		static int sTicks = 0;
		sTicks++;
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
			if (theApp->mGameSelector && theApp->mGameScene == GameScenes::SCENE_MENU && sTicks > 100)
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
				theApp->mSeedChooserScreen->PickRandomSeeds();
				sTicks = 0;
			}
			break;
		case 3:
			if (theApp->mBoard && theApp->mGameScene == GameScenes::SCENE_PLAYING && sTicks > 250)
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
			if (theApp->mBoard && sTicks % 200 == 0 && sTicks <= 3000)
			{
				for (Zombie* aZombie : theApp->mBoard->mZombies)
				{
					if (!aZombie->mDead)
						PvzpLogLn("ModTest: t={} zombie type {} row {} x {:.0f} velX {:.2f} body {}", sTicks, static_cast<int>(aZombie->mZombieType), aZombie->mRow, aZombie->mPosX, aZombie->mVelX, aZombie->mBodyHealth);
				}
				int aCount[16] = {};
				for (Projectile* aProjectile : theApp->mBoard->mProjectiles)
				{
					if (!aProjectile->mDead && aProjectile->mProjectileType >= 0 && aProjectile->mProjectileType < 16)
						aCount[aProjectile->mProjectileType]++;
				}
				PvzpLogLn("ModTest: t={} projectiles pea {} fireball {}", sTicks, aCount[ProjectileType::PROJECTILE_PEA], aCount[ProjectileType::PROJECTILE_FIREBALL]);
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
