#include "ModContent.h"
#include "LawnApp.h"
#include "Lawn/Zombie.h"
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
	default:
		break;
	}
	theZombie->mBodyMaxHealth = theZombie->mBodyHealth;
	theZombie->mHelmMaxHealth = theZombie->mHelmHealth;
}

namespace
{
	Sexy::Image* LoadModImage(const char* theName)
	{
		char* aBase = SDL_GetBasePath();
		std::filesystem::path aBasePath = aBase ? aBase : "";
		SDL_free(aBase);
		for (const std::filesystem::path& aFolder : { aBasePath / "mods", aBasePath / ".." / "Resources" / "mods" })
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
	static const NutImages sNone;
	return theSeedType == SeedType::SEED_ICE_NUT ? sIceNut : theSeedType == SeedType::SEED_PEA_NUT ? sPeaNut : sNone;
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
