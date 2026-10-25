#ifndef __MODCONTENT_H__
#define __MODCONTENT_H__

#include "ConstEnums.h"

class LawnApp;

// New plants and zombies added by the mod. Each one is a variant of an original type: it shares the
// original's case labels in the game code and changes a few things (projectile, speed, color, health).
// Their enum values sit at the end of SeedType / ZombieType so the originals keep their numbers.
namespace ModContent
{
	constexpr SeedType		FIRST_MOD_SEED = SeedType::SEED_FIRE_PEASHOOTER;
	constexpr ZombieType	FIRST_MOD_ZOMBIE = ZombieType::ZOMBIE_SPEEDY;

	constexpr bool IsModSeed(SeedType theSeedType)
	{
		return theSeedType >= FIRST_MOD_SEED && theSeedType < SeedType::NUM_SEED_TYPES;
	}

	constexpr bool IsModZombie(ZombieType theZombieType)
	{
		return theZombieType >= FIRST_MOD_ZOMBIE && theZombieType < ZombieType::NUM_ZOMBIE_TYPES;
	}

	constexpr float SPEEDY_ZOMBIE_SPEED = 2.5f;		// times a normal zombie's walking speed

	void	RegisterStrings(LawnApp* theApp);		// names, tooltips and almanac text, after LawnStrings loads
}

#endif
