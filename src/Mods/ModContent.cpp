#include "ModContent.h"
#include "LawnApp.h"
#include "Lawn/Zombie.h"
#include "Lawn/Plant.h"
#include "Lawn/Board.h"
#include <cstdlib>
#include "SexyAppFramework/graphics/GLImage.h"
#include <SDL.h>
#include <filesystem>
#include <string>

namespace
{
	struct ModString
	{
		const char*	mKey;
		const char*	mValue;
	};

	// Keys follow LawnStrings.txt: NAME, NAME_TOOLTIP, NAME_DESCRIPTION_HEADER, NAME_DESCRIPTION.
	const ModString gModStrings[] = {
		{ "FIRE_PEASHOOTER", "Lanzafuego" },
		{ "FIRE_PEASHOOTER_TOOLTIP", "Dispara guisantes de fuego" },
		{ "FIRE_PEASHOOTER_DESCRIPTION_HEADER", "Lanzafuego dispara guisantes de fuego que queman al zombi y a los que tiene cerca." },
		{ "FIRE_PEASHOOTER_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dano:{STAT} doble, en area\n{SHORTLINE}\n"
			"{FLAVOR}Lanzafuego se paso una noche entera junto a la Antorcha y ya no quiso volver a los guisantes frios." },

		{ "ICE_NUT", "Hielonuez" },
		{ "ICE_NUT_TOOLTIP", "Congela a quien la muerde" },
		{ "ICE_NUT_DESCRIPTION_HEADER", "Hielonuez aguanta como una nuez y congela a los zombis que la muerden." },
		{ "ICE_NUT_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} alta\n{KEYWORD}Especial:{STAT} ralentiza al que muerde\n{SHORTLINE}\n"
			"{FLAVOR}Hielonuez paso el invierno en el congelador del vecino. Dice que no se queja, pero nadie le cree." },

		{ "PEA_NUT", "Nuez tiradora" },
		{ "PEA_NUT_TOOLTIP", "Aguanta y dispara guisantes" },
		{ "PEA_NUT_DESCRIPTION_HEADER", "Nuez tiradora es una nuez que aprendio a escupir guisantes." },
		{ "PEA_NUT_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} alta\n{KEYWORD}Dano:{STAT} normal\n{SHORTLINE}\n"
			"{FLAVOR}Nadie sabe de donde saca los guisantes. Ella tampoco, y prefiere no preguntar." },

		{ "GOLD_SUNFLOWER", "Girasol dorado" },
		{ "GOLD_SUNFLOWER_TOOLTIP", "Da el doble de sol" },
		{ "GOLD_SUNFLOWER_DESCRIPTION_HEADER", "Girasol dorado produce dos soles cada vez." },
		{ "GOLD_SUNFLOWER_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Produccion de sol:{STAT} doble\n{SHORTLINE}\n"
			"{FLAVOR}Se pule con oro cada dia. Los demas girasoles dicen que es presumido; el dice que es ahorro." },

		{ "MOD_MODES_TITLE", "Modos mod" },
		{ "MOD_PAGE_BUTTON", "Mods" },
		{ "MINIGAMES_PAGE_BUTTON", "Minijuegos" },
		{ "MOD_INVASION", "Invasion mod" },
		{ "MOD_GHOST_NIGHT", "Noche fantasma" },
		{ "MOD_GIANTS", "Gigantes" },
		{ "MOD_CONVEYOR", "Cinta mod" },
		{ "MOD_POOL_CHAOS", "Caos en la piscina" },

		{ "POISON_PEASHOOTER", "Lanzaveneno" },
		{ "POISON_PEASHOOTER_TOOLTIP", "Sus guisantes envenenan" },
		{ "POISON_PEASHOOTER_DESCRIPTION_HEADER", "Lanzaveneno dispara guisantes que envenenan: el zombi sigue perdiendo vida un rato." },
		{ "POISON_PEASHOOTER_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dano:{STAT} normal + veneno 4 s\n{SHORTLINE}\n"
			"{FLAVOR}Comio algo raro en el jardin del vecino. Desde entonces, todo lo que escupe es morado." },
		{ "SPIKY_NUT", "Nuez espinosa" },
		{ "SPIKY_NUT_TOOLTIP", "Hace dano al que la muerde" },
		{ "SPIKY_NUT_DESCRIPTION_HEADER", "Nuez espinosa aguanta como una nuez y lastima a cada zombi que la muerde." },
		{ "SPIKY_NUT_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} alta\n{KEYWORD}Especial:{STAT} espinas\n{SHORTLINE}\n"
			"{FLAVOR}Se crio junto a un cactus. Aprendio que la mejor defensa es que te duela morderla." },
		{ "HEAL_FLOWER", "Girasol sanador" },
		{ "HEAL_FLOWER_TOOLTIP", "Cura a las plantas de alrededor" },
		{ "HEAL_FLOWER_DESCRIPTION_HEADER", "Girasol sanador no da sol: cada pocos segundos cura a las plantas que lo rodean." },
		{ "HEAL_FLOWER_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Curacion:{STAT} 80 cada 2.5 s, en 3x3\n{SHORTLINE}\n"
			"{FLAVOR}Estudio enfermeria por correspondencia. Las nueces la adoran." },
		{ "ICE_CHERRY", "Cereza de hielo" },
		{ "ICE_CHERRY_TOOLTIP", "Explota y congela" },
		{ "ICE_CHERRY_DESCRIPTION_HEADER", "Cereza de hielo explota congelando a todos los zombis cercanos." },
		{ "ICE_CHERRY_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dano:{STAT} leve, congela en area\n{SHORTLINE}\n"
			"{FLAVOR}La prima friolenta de la Petacereza. No le gusta el ruido, pero le encanta el silencio que deja." },

		{ "SPEEDY_ZOMBIE", "Zombi veloz" },
		{ "SPEEDY_ZOMBIE_DESCRIPTION_HEADER", "El Zombi veloz camina dos veces y media mas rapido que uno normal." },
		{ "SPEEDY_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja {270}\n{KEYWORD}Velocidad:{STAT} muy rapida\n{SHORTLINE}\n"
			"{FLAVOR}Se tomo seis cafes antes de salir de la tumba. No sabe a donde va, pero va a llegar primero." },

		{ "GIANT_PAIL_ZOMBIE", "Caracubo gigante" },
		{ "GIANT_PAIL_ZOMBIE_DESCRIPTION_HEADER", "El Caracubo gigante es enorme, lento y aguanta tres veces mas." },
		{ "GIANT_PAIL_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} altisima\n{KEYWORD}Velocidad:{STAT} lenta\n{SHORTLINE}\n"
			"{FLAVOR}Encontro el cubo de la pintura de la casa. No le queda, pero se lo puso igual." },

		{ "ICE_ZOMBIE", "Zombi de hielo" },
		{ "ICE_ZOMBIE_DESCRIPTION_HEADER", "El Zombi de hielo no se congela ni se ralentiza, y aguanta mas." },
		{ "ICE_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} media\n{KEYWORD}Especial:{STAT} inmune al frio\n{SHORTLINE}\n"
			"{FLAVOR}Vivia dentro de una nevera. Los guisantes de hielo le dan cosquillas." },

		{ "HEALER_ZOMBIE", "Zombi medico" },
		{ "HEALER_ZOMBIE_DESCRIPTION_HEADER", "El Zombi medico cura a los zombis que tiene cerca cada pocos segundos." },
		{ "HEALER_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja\n{KEYWORD}Especial:{STAT} cura a los de su zona\n{SHORTLINE}\n"
			"{FLAVOR}Nunca termino la carrera, pero tiene la bata y mucha confianza." },
		{ "KAMIKAZE_ZOMBIE", "Zombi kamikaze" },
		{ "KAMIKAZE_ZOMBIE_DESCRIPTION_HEADER", "Al morir, el Zombi kamikaze explota y dana a las plantas cercanas. Matalo lejos." },
		{ "KAMIKAZE_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja\n{KEYWORD}Especial:{STAT} explota al morir\n{SHORTLINE}\n"
			"{FLAVOR}Se trago una petacereza entera. No se lo recomienda a nadie." },
		{ "SPLITTER_ZOMBIE", "Zombi divisor" },
		{ "SPLITTER_ZOMBIE_DESCRIPTION_HEADER", "Al morir, el Zombi divisor se parte en dos minizombis rapidos." },
		{ "SPLITTER_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} media\n{KEYWORD}Especial:{STAT} se divide\n{SHORTLINE}\n"
			"{FLAVOR}Siempre dijo que valia por dos. Resulta que era literal." },
		{ "MINI_ZOMBIE", "Minizombi" },
		{ "MINI_ZOMBIE_DESCRIPTION_HEADER", "Pequeno, debil y muy rapido. Sale de un Zombi divisor." },
		{ "MINI_ZOMBIE_DESCRIPTION", "{SHORTLINE}\n{KEYWORD}Dureza:{STAT} muy baja\n{SHORTLINE}\n{FLAVOR}La mitad de un zombi, el doble de prisa." },
		{ "HOPPER_ZOMBIE", "Zombi saltacarril" },
		{ "HOPPER_ZOMBIE_DESCRIPTION_HEADER", "El Zombi saltacarril cambia de fila cada pocos segundos." },
		{ "HOPPER_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja\n{KEYWORD}Especial:{STAT} cambia de fila\n{SHORTLINE}\n"
			"{FLAVOR}No se decide por ninguna fila. En vida tampoco se decidia por nada." },

		{ "GHOST_ZOMBIE", "Zombi fantasma" },
		{ "GHOST_ZOMBIE_DESCRIPTION_HEADER", "Los disparos rectos atraviesan al Zombi fantasma. Usa catapultas o explosiones." },
		{ "GHOST_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja\n{KEYWORD}Especial:{STAT} los guisantes lo atraviesan\n{SHORTLINE}\n"
			"{FLAVOR}Ya estaba muerto antes de ser zombi. Es complicado." },
	};
}

void ModContent::InitZombieVariant(Zombie* theZombie)
{
	switch (theZombie->mZombieType)
	{
	case ZombieType::ZOMBIE_GIANT_PAIL:
		theZombie->mScaleZombie = GIANT_PAIL_SCALE;
		theZombie->mBodyHealth *= GIANT_PAIL_HEALTH;
		theZombie->mHelmHealth *= GIANT_PAIL_HEALTH;
		break;
	case ZombieType::ZOMBIE_ICE:
		theZombie->mBodyHealth = theZombie->mBodyHealth * 3 / 2;
		break;
	case ZombieType::ZOMBIE_HEALER:
		theZombie->mBodyHealth = 300;
		break;
	case ZombieType::ZOMBIE_SPLITTER:
		theZombie->mBodyHealth = 400;
		break;
	case ZombieType::ZOMBIE_MINI:
		theZombie->mScaleZombie = MINI_SCALE;
		theZombie->mBodyHealth = 100;
		break;
	default:
		break;
	}
	if (gLawnApp->mGameMode == GameMode::GAMEMODE_MOD_GIANTS && theZombie->mZombieType != ZombieType::ZOMBIE_BOSS)
	{
		theZombie->mScaleZombie *= GIANTS_MODE_SCALE;
		theZombie->mBodyHealth *= 2;
		theZombie->mHelmHealth *= 2;
		theZombie->mShieldHealth *= 2;
		theZombie->mFlyingHealth *= 2;
	}
	theZombie->mBodyMaxHealth = theZombie->mBodyHealth;
	theZombie->mHelmMaxHealth = theZombie->mHelmHealth;
	theZombie->mShieldMaxHealth = theZombie->mShieldHealth;
	theZombie->mFlyingMaxHealth = theZombie->mFlyingHealth;
	theZombie->mModPoisonCounter = 0;
	theZombie->mModTimer = 0;
	theZombie->mModDeathDone = false;
}

namespace
{
	void SpawnSplitterMinis(Zombie* theZombie)
	{
		for (int i = 0; i < 2; i++)
		{
			Zombie* aMini = theZombie->mBoard->AddZombieInRow(ZombieType::ZOMBIE_MINI, theZombie->mRow, theZombie->mFromWave);
			if (aMini)
			{
				aMini->mPosX = theZombie->mPosX + (i == 0 ? -15.0f : 25.0f);
				aMini->mX = static_cast<int>(aMini->mPosX);
			}
		}
	}

	void KamikazeExplode(Zombie* theZombie)
	{
		Board* aBoard = theZombie->mBoard;
		Rect aRect = theZombie->GetZombieRect();
		int aCenterX = aRect.mX + aRect.mWidth / 2;
		int aCenterY = aRect.mY + aRect.mHeight / 2;
		for (Plant* aPlant : aBoard->mPlants)
		{
			if (aPlant->mDead || std::abs(aPlant->mRow - theZombie->mRow) > 1)
				continue;
			if (!GetCircleRectOverlap(aCenterX, aCenterY, 115, aPlant->GetPlantRect()))
				continue;
			aPlant->mPlantHealth -= ModContent::KAMIKAZE_DAMAGE;
			if (aPlant->mPlantHealth <= 0)
				aPlant->Die();
		}
		gLawnApp->PlayFoley(FoleyType::FOLEY_CHERRYBOMB);
		gLawnApp->AddPvzpParticle(aCenterX, aCenterY, static_cast<int>(RenderLayer::RENDER_LAYER_TOP), ParticleEffect::PARTICLE_POWIE);
		aBoard->ShakeBoard(3, -4);
	}
}

bool ModContent::IsPoisonColored(const Zombie* theZombie)
{
	return theZombie->mModPoisonCounter > 0;
}

void ModContent::UpdateZombie(Zombie* theZombie)
{
	if (!theZombie->IsOnBoard() || theZombie->IsDeadOrDying() || gLawnApp->mGameScene != GameScenes::SCENE_PLAYING)
		return;
	Board* aBoard = theZombie->mBoard;
	theZombie->mModTimer++;

	if (theZombie->mModPoisonCounter > 0)
	{
		theZombie->mModPoisonCounter--;
		if (theZombie->mModPoisonCounter % 50 == 0)
			theZombie->TakeDamage(POISON_DAMAGE, 1U << DamageFlags::DAMAGE_DOESNT_CAUSE_FLASH);
	}

	switch (theZombie->mZombieType)
	{
	case ZombieType::ZOMBIE_HEALER:
		if (theZombie->mModTimer % HEALER_INTERVAL == 0)
		{
			for (Zombie* anOther : aBoard->mZombies)
			{
				if (anOther == theZombie || anOther->mDead || anOther->IsDeadOrDying() || anOther->mMindControlled)
					continue;
				if (std::abs(anOther->mRow - theZombie->mRow) > 1 || std::abs(anOther->mPosX - theZombie->mPosX) > 150.0f)
					continue;
				if (anOther->mBodyHealth < anOther->mBodyMaxHealth)
				{
					anOther->mBodyHealth = std::min(anOther->mBodyHealth + HEALER_AMOUNT, anOther->mBodyMaxHealth);
					anOther->mJustGotShotCounter = 25;	// a white flash marks the heal
				}
			}
		}
		break;
	case ZombieType::ZOMBIE_HOPPER:
		// The garlic reaction without the disgust pause: straight to "walk to another lane"
		if (theZombie->mModTimer % HOPPER_INTERVAL == 0 && !theZombie->mIsEating && !theZombie->mYuckyFace &&
			theZombie->mZombiePhase == ZombiePhase::PHASE_ZOMBIE_NORMAL && theZombie->mHasHead)
		{
			theZombie->mYuckyFace = true;
			theZombie->mYuckyFaceCounter = 169;
		}
		break;
	default:
		break;
	}
}

void ModContent::OnZombieKilled(Zombie* theZombie)
{
	// The game can call DropLoot twice (when the head falls off and when the body drops)
	if (!theZombie->IsOnBoard() || theZombie->mModDeathDone)
		return;
	theZombie->mModDeathDone = true;
	// Spawning and killing while the board loops over zombies or plants is fine: the arrays have a fixed size
	// and the loops skip whatever is dead (the Gargantuar throws its Imp the same way).
	if (theZombie->mZombieType == ZombieType::ZOMBIE_SPLITTER)
		SpawnSplitterMinis(theZombie);
	else if (theZombie->mZombieType == ZombieType::ZOMBIE_KAMIKAZE)
		KamikazeExplode(theZombie);
}

void ModContent::UpdatePlant(Plant* thePlant)
{
	if (!thePlant->IsInPlay() || gLawnApp->mGameScene != GameScenes::SCENE_PLAYING)
		return;
	if (thePlant->mSeedType != SeedType::SEED_HEAL_FLOWER)
		return;
	thePlant->mModTimer++;
	if (thePlant->mModTimer % HEAL_FLOWER_INTERVAL != 0)
		return;
	for (Plant* anOther : thePlant->mBoard->mPlants)
	{
		if (anOther->mDead || std::abs(anOther->mRow - thePlant->mRow) > 1 || std::abs(anOther->mPlantCol - thePlant->mPlantCol) > 1)
			continue;
		if (anOther->mPlantHealth < anOther->mPlantMaxHealth)
		{
			anOther->mPlantHealth = std::min(anOther->mPlantHealth + HEAL_FLOWER_AMOUNT, anOther->mPlantMaxHealth);
			anOther->mBeghouledFlashCountdown = 50;	// the glow Beghouled uses, marks the heal
		}
	}
}

void ModContent::IceCherryExplode(Plant* thePlant)
{
	Board* aBoard = thePlant->mBoard;
	int aPosX = thePlant->mX + thePlant->mWidth / 2;
	int aPosY = thePlant->mY + thePlant->mHeight / 2;
	for (Zombie* aZombie : aBoard->mZombies)
	{
		if (aZombie->mDead || aZombie->IsDeadOrDying() || std::abs(aZombie->mRow - thePlant->mRow) > 1 || !aZombie->EffectedByDamage(127))
			continue;
		if (!GetCircleRectOverlap(aPosX, aPosY, 115, aZombie->GetZombieRect()))
			continue;
		aZombie->TakeDamage(ICE_CHERRY_DAMAGE, 1U << DamageFlags::DAMAGE_DOESNT_CAUSE_FLASH);
		if (!aZombie->IsDeadOrDying() && aZombie->CanBeFrozen())
			aZombie->HitIceTrap();
	}
	gLawnApp->PlayFoley(FoleyType::FOLEY_FROZEN);
	gLawnApp->AddPvzpParticle(aPosX, aPosY, static_cast<int>(RenderLayer::RENDER_LAYER_TOP), ParticleEffect::PARTICLE_ICE_TRAP);
	aBoard->ShakeBoard(2, -3);
}

namespace
{
	Sexy::Image* LoadModImage(const char* theName)
	{
		char* aBase = SDL_GetBasePath();
		std::filesystem::path aBasePath = aBase ? aBase : "";
		SDL_free(aBase);
		// Android has no base path: the resource importer puts mods/ next to main.pak, in the resource dir.
		std::filesystem::path aResourcePath = gLawnApp->mResourceDir;
		for (const std::filesystem::path& aFolder : { aBasePath / "mods", aBasePath / ".." / "Resources" / "mods", aResourcePath / "mods" })
		{
			std::filesystem::path aFile = aFolder / theName;
			if (std::filesystem::is_regular_file(aFile))
				return gLawnApp->GetImage(aFile.string());
		}
		return nullptr;
	}

	ModContent::NutImages LoadNutImages(const char* thePrefix)
	{
		std::string aPrefix = thePrefix;
		ModContent::NutImages anImages;
		anImages.mBody = LoadModImage((aPrefix + "_body.png").c_str());
		anImages.mCracked1 = LoadModImage((aPrefix + "_cracked1.png").c_str());
		anImages.mCracked2 = LoadModImage((aPrefix + "_cracked2.png").c_str());
		if (!anImages.mBody || !anImages.mCracked1 || !anImages.mCracked2)
			anImages = {};
		return anImages;
	}
}

const ModContent::NutImages& ModContent::GetNutImages(SeedType theSeedType)
{
	static const NutImages sIceNut = LoadNutImages("IceNut");
	static const NutImages sPeaNut = LoadNutImages("PeaNut");
	static const NutImages sSpikyNut = LoadNutImages("SpikyNut");
	static const NutImages sNone;
	switch (theSeedType)
	{
	case SeedType::SEED_ICE_NUT:	return sIceNut;
	case SeedType::SEED_PEA_NUT:	return sPeaNut;
	case SeedType::SEED_SPIKY_NUT:	return sSpikyNut;
	default:						return sNone;
	}
}

bool ModContent::HasNutImages(SeedType theSeedType)
{
	return GetNutImages(theSeedType).mBody != nullptr;
}

void ModContent::RegisterStrings(LawnApp* theApp)
{
	for (const ModString& aString : gModStrings)
		theApp->mStringProperties.try_emplace(aString.mKey, aString.mValue);
}
