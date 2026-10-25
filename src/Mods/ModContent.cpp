#include "ModContent.h"
#include "LawnApp.h"

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

		{ "SPEEDY_ZOMBIE", "Zombi veloz" },
		{ "SPEEDY_ZOMBIE_DESCRIPTION_HEADER", "El Zombi veloz camina dos veces y media mas rapido que uno normal." },
		{ "SPEEDY_ZOMBIE_DESCRIPTION",
			"{SHORTLINE}\n{KEYWORD}Dureza:{STAT} baja {270}\n{KEYWORD}Velocidad:{STAT} muy rapida\n{SHORTLINE}\n"
			"{FLAVOR}Se tomo seis cafes antes de salir de la tumba. No sabe a donde va, pero va a llegar primero." },
	};
}

void ModContent::RegisterStrings(LawnApp* theApp)
{
	for (const ModString& aString : gModStrings)
		theApp->mStringProperties.try_emplace(aString.mKey, aString.mValue);
}
