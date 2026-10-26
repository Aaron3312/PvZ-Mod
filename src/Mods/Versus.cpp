#include "Versus.h"
#include "LawnApp.h"
#include "Resources.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/PvzpDebug.h"
#include "PvzpLib/PvzpFoley.h"
#include "graphics/Graphics.h"
#include "misc/KeyCodes.h"
#include <SDL.h>
#include <algorithm>
#include <format>

using namespace Sexy;

namespace ModVersus
{
	namespace
	{
		constexpr RosterEntry ROSTER[NUM_ROSTER] = {
			{ ZombieType::ZOMBIE_NORMAL, 50, "Normal" },
			{ ZombieType::ZOMBIE_TRAFFIC_CONE, 75, "Cono" },
			{ ZombieType::ZOMBIE_PAIL, 125, "Cubo" },
			{ ZombieType::ZOMBIE_POLEVAULTER, 75, "Pertiga" },
			{ ZombieType::ZOMBIE_SPEEDY, 75, "Veloz" },
			{ ZombieType::ZOMBIE_SPLITTER, 100, "Divisor" },
			{ ZombieType::ZOMBIE_KAMIKAZE, 100, "Kamikaze" },
			{ ZombieType::ZOMBIE_HEALER, 125, "Medico" },
			{ ZombieType::ZOMBIE_FOOTBALL, 175, "Futbol" },
		};

		// The zombie player's side. Not saved: a versus level isn't saved mid-level (Board::NeedSaveGame).
		int			sBrains = START_BRAINS;
		int			sLane = 2;
		int			sLastSend = -SEND_COOLDOWN;
		NetHooks	sNet;

		// PVZ_MOD_TEST_VERSUS: the test plays the zombie side itself, and the real keyboard is ignored
		// (the test window takes the focus while someone may be typing elsewhere).
		bool IsTestPlayer()
		{
			static const bool sTest = SDL_getenv("PVZ_MOD_TEST_VERSUS") != nullptr;
			return sTest;
		}

		// PVZ_MOD_TEST_VERSUS_SECONDS shortens the match so a test can reach the plants' win.
		int DurationTicks()
		{
			static const int sDuration = [] {
				const char* aSeconds = SDL_getenv("PVZ_MOD_TEST_VERSUS_SECONDS");
				return aSeconds && SDL_atoi(aSeconds) > 0 ? SDL_atoi(aSeconds) * 100 : DURATION_TICKS;
			}();
			return sDuration;
		}

		bool IsLane(Board* theBoard, int theRow)
		{
			return theRow >= 0 && theRow < MAX_GRID_SIZE_Y && theBoard->mPlantRow[theRow] != PlantRowType::PLANTROW_DIRT;
		}

		void MoveLane(Board* theBoard, int theStep)
		{
			for (int aRow = sLane + theStep; aRow >= 0 && aRow < MAX_GRID_SIZE_Y; aRow += theStep)
			{
				if (IsLane(theBoard, aRow))
				{
					sLane = aRow;
					return;
				}
			}
		}

		bool IsMatchOver(Board* theBoard)
		{
			return theBoard->mLevelAwardSpawned || theBoard->mApp->mGameScene != GameScenes::SCENE_PLAYING;
		}

		int KeySlot(int theKey)
		{
			if (theKey >= '1' && theKey <= '9')
				return theKey - '1';
			if (theKey >= KeyCode::KEYCODE_NUMPAD1 && theKey <= KeyCode::KEYCODE_NUMPAD1 + 8)
				return theKey - KeyCode::KEYCODE_NUMPAD1;
			return -1;
		}

		bool IsZombieKey(int theKey)
		{
			return theKey == KeyCode::KEYCODE_UP || theKey == 'W' || theKey == KeyCode::KEYCODE_DOWN || theKey == 'S' || KeySlot(theKey) >= 0;
		}

		// The zombie player's keys, from this keyboard, the test or the other computer.
		bool HandleKey(Board* theBoard, int theKey)
		{
			if (theKey == KeyCode::KEYCODE_UP || theKey == 'W')
			{
				MoveLane(theBoard, -1);
				return true;
			}
			if (theKey == KeyCode::KEYCODE_DOWN || theKey == 'S')
			{
				MoveLane(theBoard, 1);
				return true;
			}
			int aSlot = KeySlot(theKey);
			if (aSlot < 0)
				return false;

			SendZombie(theBoard, aSlot);
			return true;
		}

		// The same award a minigame drops when its last zombie dies (Zombie::TrySpawnLevelAward).
		void PlantsWin(Board* theBoard)
		{
			LawnApp* aApp = theBoard->mApp;
			theBoard->mLevelAwardSpawned = true;
			aApp->mBoardResult = BoardResult::BOARDRESULT_WON;
			theBoard->RemoveAllZombies();

			CoinType aCoinType = CoinType::COIN_TROPHY;
			if (aApp->HasBeatenChallenge(aApp->mGameMode))
				aCoinType = CoinType::COIN_AWARD_MONEY_BAG;
			else if (aApp->TrophiesNeedForGoldSunflower() == 1)
				aCoinType = CoinType::COIN_AWARD_GOLD_SUNFLOWER;
			aApp->PlayFoley(FoleyType::FOLEY_SPAWN_SUN);
			theBoard->AddCoin(400, 280, aCoinType, CoinMotion::COIN_MOTION_COIN);
			theBoard->DisplayAdvice("Se acabo el tiempo: ganan las plantas!", MessageStyle::MESSAGE_STYLE_HINT_FAST, AdviceType::ADVICE_NONE);
			PvzpLogLn("Versus: plants win at tick {}", theBoard->mMainCounter);
		}

		// The test plants a defense and plays the zombie side through the key handler, so a muted test can
		// watch a whole match (see PVZ_MOD_TEST=mode6 in Mods.cpp).
		void UpdateTestPlayer(Board* theBoard)
		{
			if (!IsTestPlayer())
				return;

			int aTick = theBoard->mMainCounter;
			if (aTick == 5)
			{
				for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
				{
					if (!IsLane(theBoard, aRow))
						continue;
					theBoard->AddPlant(0, aRow, SeedType::SEED_SUNFLOWER);
					theBoard->AddPlant(1, aRow, SeedType::SEED_PEASHOOTER);
					if (aRow % 2 == 0)
						theBoard->AddPlant(4, aRow, SeedType::SEED_WALLNUT);
				}
			}
			if (aTick > GRACE_TICKS && aTick % 150 == 0)
			{
				int aSend = (aTick - GRACE_TICKS) / 150;
				int aTarget = aSend % 5;
				for (int i = 0; i < MAX_GRID_SIZE_Y && sLane != aTarget; i++)
					HandleKey(theBoard, sLane < aTarget ? 'S' : KeyCode::KEYCODE_UP);
				int aSlot = aSend % NUM_ROSTER;
				int aBrains = sBrains;
				HandleKey(theBoard, '1' + aSlot);
				PvzpLogLn("Versus test: tick {} lane {} slot {} ({}) brains {} -> {}", aTick, sLane, aSlot + 1, ROSTER[aSlot].mName, aBrains, sBrains);
			}
		}
	}

	const RosterEntry& GetRosterEntry(int theSlot)
	{
		return ROSTER[theSlot];
	}

	void SetNetHooks(const NetHooks& theHooks)
	{
		sNet = theHooks;
	}

	int GetBrains()
	{
		return sBrains;
	}

	int GetLane()
	{
		return sLane;
	}

	void StartLevel(Board* theBoard)
	{
		if (!IsVersus(theBoard->mApp->mGameMode))
			return;

		sBrains = START_BRAINS;
		sLane = 2;
		sLastSend = -SEND_COOLDOWN;
		if (!IsLane(theBoard, sLane))
			MoveLane(theBoard, 1);

		std::string aAdvice = "Versus: el jugador zombi elige carril con W/S o las flechas y manda zombis con 1-9";
		std::string aJoin = sNet.mJoinText ? sNet.mJoinText() : "";
		if (!aJoin.empty())
			aAdvice = std::format("Versus. Jugador zombi en otra Mac: abre {} en el navegador. O en esta Mac: W/S y 1-9", aJoin);
		theBoard->DisplayAdvice(aAdvice, MessageStyle::MESSAGE_STYLE_HINT_TALL_LONG, AdviceType::ADVICE_NONE);
	}

	void Update(Board* theBoard)
	{
		if (!IsVersus(theBoard->mApp->mGameMode) || IsMatchOver(theBoard))
			return;

		int aTick = theBoard->mMainCounter;
		if (aTick > 0 && aTick % BRAIN_INTERVAL == 0)
			sBrains += BRAIN_INCOME;

		int aKey = 0;
		while (sNet.mPopKey && sNet.mPopKey(&aKey))
		{
			int aBrains = sBrains;
			HandleKey(theBoard, aKey);
			PvzpLogLn("Versus: remote key {} at tick {}: lane {} brains {} -> {}", aKey, aTick, sLane, aBrains, sBrains);
		}

		UpdateTestPlayer(theBoard);

		if (aTick >= DurationTicks())
			PlantsWin(theBoard);
	}

	bool SendZombie(Board* theBoard, int theSlot)
	{
		if (theSlot < 0 || theSlot >= NUM_ROSTER)
			return false;

		const RosterEntry& aEntry = ROSTER[theSlot];
		int aTick = theBoard->mMainCounter;
		if (aTick < GRACE_TICKS || aTick - sLastSend < SEND_COOLDOWN || sBrains < aEntry.mCost || !IsLane(theBoard, sLane))
			return false;

		if (theBoard->AddZombieInRow(aEntry.mZombieType, sLane, theBoard->mCurrentWave) == nullptr)
			return false;

		sBrains -= aEntry.mCost;
		sLastSend = aTick;
		return true;
	}

	bool KeyDown(Board* theBoard, int theKey)
	{
		if (!IsVersus(theBoard->mApp->mGameMode) || IsMatchOver(theBoard) || !IsZombieKey(theKey))
			return false;
		if (IsTestPlayer())
			return true;	// swallowed: only the test plays the zombies
		return HandleKey(theBoard, theKey);
	}

	void Draw(Board* theBoard, Graphics* g)
	{
		if (!IsVersus(theBoard->mApp->mGameMode))
			return;

		int aTick = theBoard->mMainCounter;
		bool aOver = IsMatchOver(theBoard);

		// The selected lane, where the zombies come in
		if (!aOver && IsLane(theBoard, sLane))
		{
			int aTop = theBoard->GridToPixelY(8, sLane);
			int aHeight = sLane + 1 < MAX_GRID_SIZE_Y ? theBoard->GridToPixelY(8, sLane + 1) - aTop : 100;
			g->SetColor(Color(255, 40, 40, 70 + (aTick / 4) % 30));
			g->FillRect(720, aTop, 80, aHeight);
			g->SetColor(Color(255, 80, 80, 200));
			g->DrawRect(720, aTop, 79, aHeight - 1);
		}

		// The zombie player's bar along the bottom: brains, clock and the roster with its keys
		g->SetColor(Color(0, 0, 0, 160));
		g->FillRect(5, 547, 588, 51);

		int aLeft = std::max(0, DurationTicks() - aTick) / 100;
		std::string aStatus = std::format("Cerebros: {}", sBrains);
		PvzpDrawString(g, aStatus, 12, 565, Sexy::FONT_HOUSEOFTERROR16, Color(255, 120, 220), DrawStringJustification::DS_ALIGN_LEFT);
		std::string aClock = aTick < GRACE_TICKS
			? std::format("Zombis en {} s", (GRACE_TICKS - aTick + 99) / 100)
			: std::format("Plantas ganan en {}:{:02}", aLeft / 60, aLeft % 60);
		PvzpDrawString(g, aClock, 588, 565, Sexy::FONT_HOUSEOFTERROR16, Color(224, 187, 98), DrawStringJustification::DS_ALIGN_RIGHT);

		// The other computer: where to connect, until someone does
		if (sNet.mHasViewer && sNet.mHasViewer())
		{
			PvzpDrawString(g, "Jugador zombi conectado", 300, 563, Sexy::FONT_BRIANNETOD12, Color(120, 255, 120), DrawStringJustification::DS_ALIGN_CENTER);
		}
		else if (sNet.mJoinText)
		{
			std::string aJoin = sNet.mJoinText();
			if (!aJoin.empty())
				PvzpDrawString(g, "Otra Mac: " + aJoin, 300, 563, Sexy::FONT_BRIANNETOD12, Color(200, 200, 255), DrawStringJustification::DS_ALIGN_CENTER);
		}

		for (int i = 0; i < NUM_ROSTER; i++)
		{
			const RosterEntry& aEntry = ROSTER[i];
			bool aCanSend = !aOver && aTick >= GRACE_TICKS && sBrains >= aEntry.mCost;
			Color aColor = aCanSend ? Color(255, 255, 255) : Color(130, 130, 130);
			int aX = 12 + i * 64;
			PvzpDrawString(g, aEntry.mName, aX, 580, Sexy::FONT_BRIANNETOD12, aColor, DrawStringJustification::DS_ALIGN_LEFT);
			PvzpDrawString(g, std::format("{}: {}", i + 1, aEntry.mCost), aX, 594, Sexy::FONT_BRIANNETOD12, aCanSend ? Color(255, 120, 220) : aColor, DrawStringJustification::DS_ALIGN_LEFT);
		}
	}
}
