#ifndef __MODCONTENT_H__
#define __MODCONTENT_H__

#include "ConstEnums.h"
#include "PvzpLib/FilterEffect.h"

class LawnApp;
class Zombie;
class Plant;
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
		case SeedType::SEED_POISON_PEASHOOTER:	return SeedType::SEED_PEASHOOTER;
		case SeedType::SEED_SPIKY_NUT:			return SeedType::SEED_WALLNUT;
		case SeedType::SEED_HEAL_FLOWER:		return SeedType::SEED_SUNFLOWER;
		case SeedType::SEED_ICE_CHERRY:			return SeedType::SEED_CHERRYBOMB;
		default:								return theSeedType;
		}
	}

	constexpr ZombieType BaseZombieType(ZombieType theZombieType)
	{
		switch (theZombieType)
		{
		case ZombieType::ZOMBIE_SPEEDY:
		case ZombieType::ZOMBIE_GHOST:
		case ZombieType::ZOMBIE_HEALER:
		case ZombieType::ZOMBIE_KAMIKAZE:
		case ZombieType::ZOMBIE_SPLITTER:
		case ZombieType::ZOMBIE_MINI:
		case ZombieType::ZOMBIE_HOPPER:			return ZombieType::ZOMBIE_NORMAL;
		case ZombieType::ZOMBIE_ICE:			return ZombieType::ZOMBIE_TRAFFIC_CONE;
		case ZombieType::ZOMBIE_GIANT_PAIL:		return ZombieType::ZOMBIE_PAIL;
		default:								return theZombieType;
		}
	}

	constexpr bool IsModGameMode(GameMode theGameMode)
	{
		return theGameMode >= GameMode::GAMEMODE_MOD_INVASION && theGameMode <= GameMode::GAMEMODE_MOD_POOL_CHAOS;
	}

	constexpr float GIANTS_MODE_SCALE = 1.4f;		// "Gigantes": every zombie this big, with twice the health
	constexpr float GIANTS_MODE_SPEED = 0.8f;

	constexpr float SPEEDY_ZOMBIE_SPEED = 2.5f;		// times a normal zombie's walking speed
	constexpr float GIANT_PAIL_SPEED = 0.6f;
	constexpr float GIANT_PAIL_SCALE = 1.5f;
	constexpr int	GIANT_PAIL_HEALTH = 3;			// times the bucket zombie's body and bucket
	constexpr int	GHOST_ALPHA = 120;				// see-through: straight shots fly through it

	// Second pack: new mechanics
	constexpr int	POISON_TICKS = 400;				// Poison Peashooter: poison lasts 4 s...
	constexpr int	POISON_DAMAGE = 10;				// ...and deals this every half second
	constexpr int	SPIKY_NUT_DAMAGE = 2;			// to the zombie per chomp, 25 chomps a second while it eats
	constexpr int	HEAL_FLOWER_INTERVAL = 250;		// Healing Sunflower: heals the 3x3 around it every 2.5 s
	constexpr int	HEAL_FLOWER_AMOUNT = 80;
	constexpr int	ICE_CHERRY_DAMAGE = 90;			// plus freezing every zombie in range
	constexpr int	HEALER_INTERVAL = 300;			// Medic Zombie: heals zombies near it every 3 s
	constexpr int	HEALER_AMOUNT = 60;
	constexpr int	KAMIKAZE_DAMAGE = 600;			// to every plant around it when it dies
	constexpr int	HOPPER_INTERVAL = 600;			// Lane Hopper: changes lane every 6 s
	constexpr float	MINI_SCALE = 0.6f;
	constexpr float	MINI_SPEED = 1.6f;

	// Recolored sprites (PvzpLib/FilterEffect.cpp), for the plants a plain tint can't color (a green plant
	// tinted purple only gets darker): set on the plant's reanims and its seed packet portrait.
	constexpr FilterEffect PlantFilter(SeedType theSeedType)
	{
		switch (theSeedType)
		{
		case SeedType::SEED_FIRE_PEASHOOTER:	return FilterEffect::FILTER_EFFECT_MOD_FIRE;
		case SeedType::SEED_POISON_PEASHOOTER:	return FilterEffect::FILTER_EFFECT_MOD_PURPLE;
		case SeedType::SEED_ICE_CHERRY:			return FilterEffect::FILTER_EFFECT_MOD_ICE;
		case SeedType::SEED_HEAL_FLOWER:		return FilterEffect::FILTER_EFFECT_MOD_PINK;
		default:								return FilterEffect::FILTER_EFFECT_NONE;
		}
	}

	bool	IsPoisonColored(const Zombie* theZombie);
	void	UpdateZombie(Zombie* theZombie);		// every tick on the board
	void	OnZombieKilled(Zombie* theZombie);		// Zombie::DropLoot, before the level award check
	void	UpdatePlant(Plant* thePlant);			// every tick on the board
	void	IceCherryExplode(Plant* thePlant);		// Plant::DoSpecial

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
	const NutImages& GetNutImages(SeedType theSeedType);	// SEED_ICE_NUT, SEED_PEA_NUT, SEED_SPIKY_NUT
	bool	HasNutImages(SeedType theSeedType);
}

#endif
