#ifndef __MODCONTENT_H__
#define __MODCONTENT_H__

#include "ConstEnums.h"

class LawnApp;
class Zombie;
namespace Sexy { class Image; }

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

	// The original each mod type is built on: switches that only need the shared behavior use this.
	constexpr SeedType BaseSeedType(SeedType theSeedType)
	{
		switch (theSeedType)
		{
		case SeedType::SEED_FIRE_PEASHOOTER:	return SeedType::SEED_PEASHOOTER;
		case SeedType::SEED_ICE_NUT:
		case SeedType::SEED_PEA_NUT:			return SeedType::SEED_WALLNUT;
		case SeedType::SEED_GOLD_SUNFLOWER:		return SeedType::SEED_SUNFLOWER;
		default:								return theSeedType;
		}
	}

	constexpr ZombieType BaseZombieType(ZombieType theZombieType)
	{
		switch (theZombieType)
		{
		case ZombieType::ZOMBIE_SPEEDY:
		case ZombieType::ZOMBIE_GHOST:			return ZombieType::ZOMBIE_NORMAL;
		case ZombieType::ZOMBIE_ICE:			return ZombieType::ZOMBIE_TRAFFIC_CONE;
		case ZombieType::ZOMBIE_GIANT_PAIL:		return ZombieType::ZOMBIE_PAIL;
		default:								return theZombieType;
		}
	}

	constexpr float SPEEDY_ZOMBIE_SPEED = 2.5f;		// times a normal zombie's walking speed
	constexpr float GIANT_PAIL_SPEED = 0.6f;
	constexpr float GIANT_PAIL_SCALE = 1.5f;
	constexpr int	GIANT_PAIL_HEALTH = 3;			// times the bucket zombie's body and bucket
	constexpr int	GHOST_ALPHA = 120;				// see-through: straight shots fly through it

	void	RegisterStrings(LawnApp* theApp);		// names, tooltips and almanac text, after LawnStrings loads
	void	InitZombieVariant(Zombie* theZombie);	// end of ZombieInitialize: size and health

	// Mod sprites made by scripts/make-mod-art.py from the player's game data, loaded from
	// <executable>/mods or <app>/Contents/Resources/mods. All null when they are missing, and the
	// plant then falls back to a color tint.
	struct NutImages
	{
		Sexy::Image*	mBody = nullptr;
		Sexy::Image*	mCracked1 = nullptr;
		Sexy::Image*	mCracked2 = nullptr;
	};
	const NutImages& GetNutImages(SeedType theSeedType);	// SEED_ICE_NUT, SEED_PEA_NUT
	bool	HasNutImages(SeedType theSeedType);
}

#endif
