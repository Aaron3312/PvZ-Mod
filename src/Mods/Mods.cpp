#include <algorithm>
#include "Mods.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"

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
