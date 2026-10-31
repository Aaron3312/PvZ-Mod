#include "Versus.h"
#include "ModContent.h"
#ifdef PVZ_HACK_MENU
#include "Mods.h"
#include "HackMenu.h"
#endif
#include "LawnApp.h"
#include "Resources.h"
#include "Lawn/Board.h"
#include "Lawn/GridItem.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/ReanimationLawn.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/PvzpDebug.h"
#include "PvzpLib/PvzpFoley.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"
#include "misc/SexyMatrix.h"
#include "misc/KeyCodes.h"
#include <SDL.h>
#include <algorithm>
#include <format>

using namespace Sexy;

namespace ModVersus
{
	namespace
	{
		// Every zombie the versus can send. Costs follow the game's own I, Zombie prices where it has one.
		constexpr RosterEntry ENTRIES[] = {
			{ ZombieType::ZOMBIE_NORMAL, 50, 400, 0, "Normal" },
			{ ZombieType::ZOMBIE_TRAFFIC_CONE, 75, 500, 0, "Cono" },
			{ ZombieType::ZOMBIE_PAIL, 125, 900, 0, "Cubo" },
			{ ZombieType::ZOMBIE_POLEVAULTER, 75, 700, 0, "Pertiga" },
			{ ZombieType::ZOMBIE_NEWSPAPER, 75, 600, 0, "Periodico" },
			{ ZombieType::ZOMBIE_DOOR, 100, 900, 0, "Puerta" },
			{ ZombieType::ZOMBIE_FLAG, 300, 6000, 2, "Oleada" },		// the flag zombie brings one zombie in every other lane
			{ ZombieType::ZOMBIE_FOOTBALL, 175, 1500, 1, "Futbol" },
			{ ZombieType::ZOMBIE_DANCER, 300, 3000, 2, "Bailarin" },
			{ ZombieType::ZOMBIE_SNORKEL, 75, 700, 0, "Buceador" },
			{ ZombieType::ZOMBIE_DOLPHIN_RIDER, 125, 1000, 0, "Delfin" },
			{ ZombieType::ZOMBIE_ZAMBONI, 175, 2000, 1, "Zomboni" },
			{ ZombieType::ZOMBIE_BOBSLED, 100, 1200, 1, "Bobsleigh" },	// needs the ice a Zomboni leaves
			{ ZombieType::ZOMBIE_JACK_IN_THE_BOX, 125, 1200, 1, "Caja" },
			{ ZombieType::ZOMBIE_BALLOON, 125, 1200, 1, "Globo" },
			{ ZombieType::ZOMBIE_DIGGER, 125, 1500, 1, "Minero" },
			{ ZombieType::ZOMBIE_POGO, 150, 1500, 1, "Saltarin" },
			{ ZombieType::ZOMBIE_BUNGEE, 125, 2000, 1, "Puenting" },	// steals a plant of the selected lane
			{ ZombieType::ZOMBIE_LADDER, 150, 1400, 1, "Escalera" },
			{ ZombieType::ZOMBIE_CATAPULT, 200, 2200, 1, "Catapulta" },
			{ ZombieType::ZOMBIE_IMP, 50, 500, 0, "Diablillo" },
			{ ZombieType::ZOMBIE_GARGANTUAR, 350, 4500, 2, "Gargantua" },
			{ ZombieType::ZOMBIE_REDEYE_GARGANTUAR, 600, 9000, 2, "Ojos rojos" },
			{ ZombieType::ZOMBIE_PEA_HEAD, 100, 900, 0, "Guisante" },
			{ ZombieType::ZOMBIE_WALLNUT_HEAD, 125, 1000, 0, "Nuez" },
			{ ZombieType::ZOMBIE_JALAPENO_HEAD, 175, 2500, 1, "Jalapeno" },
			{ ZombieType::ZOMBIE_GATLING_HEAD, 200, 2000, 1, "Gatling" },
			{ ZombieType::ZOMBIE_SQUASH_HEAD, 150, 1800, 1, "Calabaza" },
			{ ZombieType::ZOMBIE_TALLNUT_HEAD, 250, 3000, 2, "Nuez alta" },
			{ ZombieType::ZOMBIE_SPEEDY, 75, 600, 0, "Veloz" },
			{ ZombieType::ZOMBIE_HOPPER, 75, 800, 0, "Saltacarril" },
			{ ZombieType::ZOMBIE_GHOST, 100, 1000, 1, "Fantasma" },
			{ ZombieType::ZOMBIE_ICE, 125, 1000, 1, "Hielo" },
			{ ZombieType::ZOMBIE_SPLITTER, 125, 1200, 1, "Divisor" },
			{ ZombieType::ZOMBIE_KAMIKAZE, 125, 1400, 1, "Kamikaze" },
			{ ZombieType::ZOMBIE_HEALER, 150, 1800, 1, "Medico" },
			{ ZombieType::ZOMBIE_GIANT_PAIL, 300, 4000, 2, "Gigante" },
		};

		// Not in any roster: the backup dancers, the ducky tube zombie, the minis and the Imp the Gargantuar
		// throws come with others; the Yeti walks away on its own and drops diamonds; Dr. Zomboss needs his own
		// stage and script.
		struct PageDef
		{
			const char*	mName;
			int			mCount;
			ZombieType	mTypes[PAGE_SLOTS];
		};

		struct StageDef
		{
			const char*	mName;
			int			mStartSun;
			int			mNumPages;
			PageDef		mPages[MAX_PAGES];
		};

		constexpr PageDef MOD_PAGE = { "Mod", 7, { ZOMBIE_SPEEDY, ZOMBIE_HOPPER, ZOMBIE_GHOST, ZOMBIE_ICE, ZOMBIE_SPLITTER, ZOMBIE_KAMIKAZE, ZOMBIE_HEALER } };

		// One per versus mode, in GameMode order: the zombies the game uses on that kind of level, the mod ones and a
		// few heavies for the last phase.
		constexpr StageDef STAGES[] = {
			{ "Dia", 50, 4, {
				{ "Clasicos", 7, { ZOMBIE_NORMAL, ZOMBIE_TRAFFIC_CONE, ZOMBIE_PAIL, ZOMBIE_POLEVAULTER, ZOMBIE_NEWSPAPER, ZOMBIE_DOOR, ZOMBIE_FLAG } },
				{ "Zombotanica", 6, { ZOMBIE_PEA_HEAD, ZOMBIE_WALLNUT_HEAD, ZOMBIE_JALAPENO_HEAD, ZOMBIE_GATLING_HEAD, ZOMBIE_SQUASH_HEAD, ZOMBIE_TALLNUT_HEAD } },
				{ "Pesados", 3, { ZOMBIE_FOOTBALL, ZOMBIE_GARGANTUAR, ZOMBIE_GIANT_PAIL } },
				MOD_PAGE } },
			{ "Noche", 100, 3, {
				{ "Clasicos", 7, { ZOMBIE_NORMAL, ZOMBIE_TRAFFIC_CONE, ZOMBIE_PAIL, ZOMBIE_POLEVAULTER, ZOMBIE_NEWSPAPER, ZOMBIE_DOOR, ZOMBIE_FLAG } },
				{ "Pesados", 4, { ZOMBIE_FOOTBALL, ZOMBIE_DANCER, ZOMBIE_GARGANTUAR, ZOMBIE_GIANT_PAIL } },
				MOD_PAGE } },
			{ "Piscina", 50, 4, {
				{ "Clasicos", 6, { ZOMBIE_NORMAL, ZOMBIE_TRAFFIC_CONE, ZOMBIE_PAIL, ZOMBIE_POLEVAULTER, ZOMBIE_NEWSPAPER, ZOMBIE_FLAG } },
				{ "Piscina", 4, { ZOMBIE_SNORKEL, ZOMBIE_DOLPHIN_RIDER, ZOMBIE_ZAMBONI, ZOMBIE_BOBSLED } },
				{ "Pesados", 3, { ZOMBIE_FOOTBALL, ZOMBIE_GARGANTUAR, ZOMBIE_GIANT_PAIL } },
				MOD_PAGE } },
			{ "Niebla", 100, 4, {
				{ "Clasicos", 4, { ZOMBIE_NORMAL, ZOMBIE_TRAFFIC_CONE, ZOMBIE_PAIL, ZOMBIE_FLAG } },
				{ "Niebla", 6, { ZOMBIE_JACK_IN_THE_BOX, ZOMBIE_BALLOON, ZOMBIE_DIGGER, ZOMBIE_POGO, ZOMBIE_DOLPHIN_RIDER, ZOMBIE_SNORKEL } },
				{ "Pesados", 3, { ZOMBIE_FOOTBALL, ZOMBIE_GARGANTUAR, ZOMBIE_GIANT_PAIL } },
				MOD_PAGE } },
			{ "Tejado", 50, 4, {
				{ "Clasicos", 5, { ZOMBIE_NORMAL, ZOMBIE_TRAFFIC_CONE, ZOMBIE_PAIL, ZOMBIE_POLEVAULTER, ZOMBIE_FLAG } },
				{ "Tejado", 6, { ZOMBIE_BUNGEE, ZOMBIE_LADDER, ZOMBIE_CATAPULT, ZOMBIE_IMP, ZOMBIE_JACK_IN_THE_BOX, ZOMBIE_POGO } },
				{ "Pesados", 4, { ZOMBIE_FOOTBALL, ZOMBIE_GARGANTUAR, ZOMBIE_REDEYE_GARGANTUAR, ZOMBIE_GIANT_PAIL } },
				MOD_PAGE } },
		};

		constexpr const char* PHASE_NAMES[NUM_PHASES] = { "Avanzada", "Asedio", "Ola final" };

		const StageDef& StageFor(GameMode theGameMode)
		{
			int aIndex = std::clamp(static_cast<int>(theGameMode) - static_cast<int>(GameMode::GAMEMODE_MOD_VERSUS), 0, static_cast<int>(std::size(STAGES)) - 1);
			return STAGES[aIndex];
		}

		const RosterEntry* FindEntry(ZombieType theZombieType)
		{
			for (const RosterEntry& aEntry : ENTRIES)
				if (aEntry.mZombieType == theZombieType)
					return &aEntry;
			return nullptr;
		}

		struct Slot
		{
			const RosterEntry*	mEntry;
			int					mPage;
			int					mReadyTick;		// recharged at this tick
		};

		enum class Winner { NONE, PLANTS, ZOMBIES };

		// A short text that floats up from where something happened (a bonus, a brain)
		struct FloatText
		{
			int			mX;
			int			mY;
			int			mTick;
			Color		mColor;
			std::string	mText;
		};
		constexpr int FLOAT_TEXT_TICKS = 150;
		constexpr int MAX_FLOAT_TEXTS = 12;
		constexpr int MESSAGE_TICKS = 500;
		constexpr int MAX_GRAVES = 32;

		// The match. Not saved: a versus level isn't saved mid-level (Board::NeedSaveGame).
		const StageDef*	sStage = &STAGES[0];
		Slot		sRoster[MAX_ROSTER];
		int			sNumRoster = 0;
		int			sPageStart[MAX_PAGES] = {};
		int			sNumPages = 0;
		int			sBrains = START_BRAINS;
		int			sLane = 2;
		int			sPage = 0;
		int			sPhase = 0;
		bool		sGraveMode = false;
		int			sGraveReady = 0;
		Winner		sWinner = Winner::NONE;
		int			sEndTick = 0;
		bool		sGameOverShown = false;
		bool		sWarnedEnd = false;
		int			sSent = 0;
		int			sKilled = 0;
		int			sPlantsEaten = 0;
		int			sBrainsEaten = 0;
		int			sBrainBonus = 0;
		int			sSunBonus = 0;
		std::string	sMessage;
		int			sMessageTick = -MESSAGE_TICKS;
		int			sMessageTicks = MESSAGE_TICKS;
		FloatText	sFloatTexts[MAX_FLOAT_TEXTS];
		int			sNumFloatTexts = 0;
		int			sGraveCells[MAX_GRAVES][2];
		int			sNumGraves = 0;
		NetHooks	sNet;

		// PVZ_MOD_TEST_VERSUS: the test plays the zombie side itself, and the real keyboard is ignored
		// (the test window takes the focus while someone may be typing elsewhere).
		bool IsTestPlayer()
		{
			static const bool sTest = SDL_getenv("PVZ_MOD_TEST_VERSUS") != nullptr;
			return sTest;
		}

		// PVZ_MOD_TEST_VERSUS_SECONDS shortens the match (the phases keep their share of it) so a test can see it all.
		int DurationTicks()
		{
			static const int sDuration = [] {
				const char* aSeconds = SDL_getenv("PVZ_MOD_TEST_VERSUS_SECONDS");
				return aSeconds && SDL_atoi(aSeconds) > 0 ? SDL_atoi(aSeconds) * 100 : DURATION_TICKS;
			}();
			return sDuration;
		}

		int GraceTicks()
		{
			return std::min(GRACE_TICKS, DurationTicks() / 8);
		}

		int PhaseStart(int thePhase)
		{
			return DurationTicks() * PHASE_START_PERCENT[thePhase] / 100;
		}

		int PhaseAt(int theTick)
		{
			for (int aPhase = NUM_PHASES - 1; aPhase > 0; aPhase--)
				if (theTick >= PhaseStart(aPhase))
					return aPhase;
			return 0;
		}

		std::string ClockText(int theTicks)
		{
			int aSeconds = std::max(0, theTicks) / 100;
			return std::format("{}:{:02}", aSeconds / 60, aSeconds % 60);
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
			return sWinner != Winner::NONE || theBoard->mLevelAwardSpawned || theBoard->mApp->mGameScene != GameScenes::SCENE_PLAYING;
		}

		// The game's rules for where a zombie can walk (Board::RowCanHaveZombieType), without the ones tied to waves.
		bool CanGoInLane(Board* theBoard, ZombieType theZombieType, int theRow)
		{
			if (!IsLane(theBoard, theRow))
				return false;
			if (theBoard->mPlantRow[theRow] == PlantRowType::PLANTROW_POOL)
			{
				if (!Zombie::ZombieTypeCanGoInPool(theZombieType) && theZombieType != ZombieType::ZOMBIE_BALLOON)
					return false;
			}
			else if (Board::IsZombieTypePoolOnly(theZombieType))
				return false;
			if (theZombieType == ZombieType::ZOMBIE_BOBSLED && theBoard->mIceTimer[theRow] == 0)
				return false;
			// Outside the pool, a dancer needs lanes on both sides for its backup dancers
			if (theZombieType == ZombieType::ZOMBIE_DANCER && !theBoard->StageHasPool())
				return theBoard->RowCanHaveZombies(theRow - 1) && theBoard->RowCanHaveZombies(theRow + 1);
			return true;
		}

		bool CanRiseFromGrave(ZombieType theZombieType)
		{
			ZombieType aBase = ModContent::BaseZombieType(theZombieType);
			return aBase == ZombieType::ZOMBIE_NORMAL || aBase == ZombieType::ZOMBIE_TRAFFIC_CONE || aBase == ZombieType::ZOMBIE_PAIL ||
				theZombieType == ZombieType::ZOMBIE_NEWSPAPER || theZombieType == ZombieType::ZOMBIE_DOOR || theZombieType == ZombieType::ZOMBIE_FOOTBALL;
		}

		// Night: the risen grave nearest the house in the lane
		GridItem* GraveInLane(Board* theBoard, int theRow)
		{
			GridItem* aBest = nullptr;
			for (GridItem* aGridItem : theBoard->mGridItems)
			{
				if (aGridItem->mDead || aGridItem->mGridItemType != GridItemType::GRIDITEM_GRAVESTONE || aGridItem->mGridY != theRow || aGridItem->mGridItemCounter < 100)
					continue;
				if (!aBest || aGridItem->mGridX < aBest->mGridX)
					aBest = aGridItem;
			}
			return aBest;
		}

		bool StageHasGraves(Board* theBoard)
		{
			return theBoard->StageHasGraveStones();
		}

		int Cost(int theIndex)
		{
			int aCost = sRoster[theIndex].mEntry->mCost;
			if (sGraveMode)
				aCost = (aCost * GRAVE_COST_PERCENT / 100 + 4) / 5 * 5;
			return aCost;
		}

		void ShowMessage(Board* theBoard, const std::string& theMessage, int theTicks = MESSAGE_TICKS)
		{
			sMessage = theMessage;
			sMessageTick = theBoard->mMainCounter;
			sMessageTicks = theTicks;
		}

		void AddFloatText(Board* theBoard, int theX, int theY, const Color& theColor, const std::string& theText)
		{
			if (sNumFloatTexts == MAX_FLOAT_TEXTS)
			{
				std::move(sFloatTexts + 1, sFloatTexts + MAX_FLOAT_TEXTS, sFloatTexts);
				sNumFloatTexts--;
			}
			sFloatTexts[sNumFloatTexts++] = { theX, theY, static_cast<int>(theBoard->mMainCounter), theColor, theText };
		}

		// A bungee goes for a plant of the selected lane instead of anywhere on the lawn (Zombie::PickBungeeZombieTarget).
		void AimBungee(Board* theBoard, Zombie* theZombie, int theRow)
		{
			int aCols[MAX_GRID_SIZE_X];
			int aCount = 0;
			for (int aCol = 0; aCol < MAX_GRID_SIZE_X; aCol++)
			{
				if (theBoard->GetGraveStoneAt(aCol, theRow) || theBoard->mGridSquareType[aCol][theRow] == GridSquareType::GRIDSQUARE_DIRT)
					continue;
				Plant* aPlant = theBoard->GetTopPlantAt(aCol, theRow, PlantPriority::TOPPLANT_BUNGEE_ORDER);
				if (!aPlant || aPlant->mSeedType == SeedType::SEED_GRAVEBUSTER || aPlant->mSeedType == SeedType::SEED_COBCANNON)
					continue;
				if (theZombie->mTargetCol == aCol && theZombie->mRow == theRow)
					return;	// already there
				if (!theBoard->BungeeIsTargetingCell(aCol, theRow))
					aCols[aCount++] = aCol;
			}
			if (aCount == 0)
				return;
			int aCol = aCols[Rand(aCount)];
			theZombie->mTargetCol = aCol;
			theZombie->SetRow(theRow);
			theZombie->mPosX = theBoard->GridToPixelX(aCol, theRow);
			theZombie->mPosY = theZombie->GetPosYBasedOnRow(theRow);
		}

		void BuildRoster(GameMode theGameMode)
		{
			sStage = &StageFor(theGameMode);
			sNumRoster = 0;
			sNumPages = sStage->mNumPages;
			for (int aPage = 0; aPage < sNumPages; aPage++)
			{
				sPageStart[aPage] = sNumRoster;
				const PageDef& aPageDef = sStage->mPages[aPage];
				for (int i = 0; i < aPageDef.mCount; i++)
				{
					const RosterEntry* aEntry = FindEntry(aPageDef.mTypes[i]);
					if (aEntry && sNumRoster < MAX_ROSTER)
						sRoster[sNumRoster++] = { aEntry, aPage, 0 };
				}
			}
		}

		int PageCount(int thePage)
		{
			return sStage->mPages[thePage].mCount;
		}

		// The house brains: one per lane, by the house (the I, Zombie brain, eaten here by Versus instead of Challenge)
		void PlaceBrains(Board* theBoard)
		{
			for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
			{
				if (!IsLane(theBoard, aRow))
					continue;
				GridItem* aBrain = theBoard->mGridItems.DataArrayAlloc();
				aBrain->mGridItemType = GridItemType::GRIDITEM_IZOMBIE_BRAIN;
				aBrain->mGridX = 0;
				aBrain->mGridY = aRow;
				aBrain->mRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_LAWN_MOWER, aRow, 1);
				aBrain->mPosX = theBoard->GridToPixelX(0, aRow) - 38;
				aBrain->mPosY = theBoard->GridToPixelY(0, aRow) + 6;
			}
		}

		GridItem* BrainAt(Board* theBoard, int theRow)
		{
			for (GridItem* aGridItem : theBoard->mGridItems)
			{
				if (!aGridItem->mDead && aGridItem->mGridItemType == GridItemType::GRIDITEM_IZOMBIE_BRAIN && aGridItem->mGridY == theRow &&
					aGridItem->mGridItemState != GridItemState::GRIDITEM_STATE_BRAIN_SQUISHED)
					return aGridItem;
			}
			return nullptr;
		}

		// The lane's own brain, or once that one is gone the nearest one left
		GridItem* BrainFor(Board* theBoard, int theRow)
		{
			for (int aDistance = 0; aDistance < MAX_GRID_SIZE_Y; aDistance++)
			{
				if (GridItem* aBrain = BrainAt(theBoard, theRow - aDistance))
					return aBrain;
				if (GridItem* aBrain = BrainAt(theBoard, theRow + aDistance))
					return aBrain;
			}
			return nullptr;
		}

		void UpdateGraves(Board* theBoard)
		{
			int aCells[MAX_GRAVES][2];
			int aCount = 0;
			for (GridItem* aGridItem : theBoard->mGridItems)
			{
				if (!aGridItem->mDead && aGridItem->mGridItemType == GridItemType::GRIDITEM_GRAVESTONE && aCount < MAX_GRAVES)
				{
					aCells[aCount][0] = aGridItem->mGridX;
					aCells[aCount][1] = aGridItem->mGridY;
					aCount++;
				}
			}
			// A grave that was there and isn't any more was cleared by a Grave Buster: sun for the plants
			for (int i = 0; i < sNumGraves; i++)
			{
				bool aStill = false;
				for (int j = 0; j < aCount && !aStill; j++)
					aStill = aCells[j][0] == sGraveCells[i][0] && aCells[j][1] == sGraveCells[i][1];
				if (aStill || sWinner != Winner::NONE)
					continue;
				int aX = theBoard->GridToPixelX(sGraveCells[i][0], sGraveCells[i][1]);
				int aY = theBoard->GridToPixelY(sGraveCells[i][0], sGraveCells[i][1]);
				for (int aSun = 0; aSun < GRAVE_BUSTED_SUN / 25; aSun++)
					theBoard->AddCoin(aX + 10 + aSun * 15, aY + 20, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN);
				sSunBonus += GRAVE_BUSTED_SUN;
				AddFloatText(theBoard, aX + 40, aY + 10, Color(255, 240, 80), std::format("+{} sol", GRAVE_BUSTED_SUN));
				PvzpLogLn("Versus: grave at {},{} cleared, +{} sun", sGraveCells[i][0], sGraveCells[i][1], GRAVE_BUSTED_SUN);
			}
			std::copy(&aCells[0][0], &aCells[0][0] + aCount * 2, &sGraveCells[0][0]);
			sNumGraves = aCount;
		}

		void AnnouncePhase(Board* theBoard)
		{
			if (sPhase == 1)
			{
				theBoard->mApp->PlaySample(Sexy::SOUND_SIREN);
				theBoard->DisplayAdvice(std::format("Fase 2: {}. Mas cerebros y zombis nuevos", PHASE_NAMES[1]), MessageStyle::MESSAGE_STYLE_BIG_MIDDLE_FAST, AdviceType::ADVICE_NONE);
			}
			else
			{
				theBoard->mApp->PlaySample(Sexy::SOUND_HUGE_WAVE);
				theBoard->DisplayAdvice(std::format("{}! Llegan los pesados", PHASE_NAMES[2]), MessageStyle::MESSAGE_STYLE_HUGE_WAVE, AdviceType::ADVICE_NONE);
			}
			PvzpLogLn("Versus: phase {} ({}) at tick {}, brains {}", sPhase + 1, PHASE_NAMES[sPhase], theBoard->mMainCounter, sBrains);
		}

		void SetPage(int thePage)
		{
			sPage = (thePage + sNumPages) % sNumPages;
		}

		// The zombie player's keys and commands, from this keyboard, the test or the other device.
		bool HandleKey(Board* theBoard, int theKey)
		{
			if (theKey >= NET_LANE && theKey < NET_LANE + MAX_GRID_SIZE_Y)
			{
				if (IsLane(theBoard, theKey - NET_LANE))
					sLane = theKey - NET_LANE;
				return true;
			}
			if (theKey >= NET_SEND && theKey < NET_SEND + sNumRoster)
			{
				SetPage(sRoster[theKey - NET_SEND].mPage);
				SendZombie(theBoard, theKey - NET_SEND);
				return true;
			}
			switch (theKey)
			{
			case KeyCode::KEYCODE_UP:
			case 'W':
				MoveLane(theBoard, -1);
				return true;
			case KeyCode::KEYCODE_DOWN:
			case 'S':
				MoveLane(theBoard, 1);
				return true;
			case 'Q':
				SetPage(sPage - 1);
				return true;
			case 'E':
				SetPage(sPage + 1);
				return true;
			case 'T':
				if (StageHasGraves(theBoard))
				{
					sGraveMode = !sGraveMode;
					ShowMessage(theBoard, sGraveMode ? "Desde la tumba: el zombi sale de la tumba del carril (cuesta mas)" : "Desde la calle");
				}
				return true;
			default:
				break;
			}

			int aSlot = -1;
			if (theKey >= '1' && theKey <= '9')
				aSlot = theKey - '1';
			else if (theKey >= KeyCode::KEYCODE_NUMPAD1 && theKey <= KeyCode::KEYCODE_NUMPAD1 + 8)
				aSlot = theKey - KeyCode::KEYCODE_NUMPAD1;
			if (aSlot < 0)
				return false;
			if (aSlot < PageCount(sPage))
				SendZombie(theBoard, sPageStart[sPage] + aSlot);
			return true;
		}

		bool IsZombieKey(int theKey)
		{
			return theKey == KeyCode::KEYCODE_UP || theKey == KeyCode::KEYCODE_DOWN || theKey == 'W' || theKey == 'S' || theKey == 'Q' || theKey == 'E' ||
				theKey == 'T' || (theKey >= '1' && theKey <= '9') || (theKey >= KeyCode::KEYCODE_NUMPAD1 && theKey <= KeyCode::KEYCODE_NUMPAD1 + 8);
		}

		// The same award a minigame drops when its last zombie dies (Zombie::TrySpawnLevelAward).
		void PlantsWin(Board* theBoard)
		{
			LawnApp* aApp = theBoard->mApp;
			sWinner = Winner::PLANTS;
			sEndTick = theBoard->mMainCounter;
			theBoard->mLevelAwardSpawned = true;
			aApp->mBoardResult = BoardResult::BOARDRESULT_WON;
			theBoard->RemoveAllZombies();

			CoinType aCoinType = CoinType::COIN_TROPHY;
			if (aApp->HasBeatenChallenge(aApp->mGameMode))
				aCoinType = CoinType::COIN_AWARD_MONEY_BAG;
			else if (aApp->TrophiesNeedForGoldSunflower() == 1)
				aCoinType = CoinType::COIN_AWARD_GOLD_SUNFLOWER;
			aApp->PlayFoley(FoleyType::FOLEY_SPAWN_SUN);
			theBoard->AddCoin(370, 330, aCoinType, CoinMotion::COIN_MOTION_COIN);
			theBoard->ClearAdvice(AdviceType::ADVICE_NONE);
			PvzpLogLn("Versus: plants win at tick {}: sent {} killed {} plants eaten {} brains eaten {}/{}", sEndTick, sSent, sKilled, sPlantsEaten, sBrainsEaten, BRAINS_TO_WIN);
		}

		void ZombiesWin(Board* theBoard)
		{
			sWinner = Winner::ZOMBIES;
			sEndTick = theBoard->mMainCounter;
			theBoard->RemoveAllZombies();
			theBoard->ClearAdvice(AdviceType::ADVICE_NONE);
			theBoard->mApp->PlaySample(Sexy::SOUND_SCREAM);
			PvzpLogLn("Versus: zombies win at tick {}: sent {} killed {} plants eaten {} brains eaten {}/{}", sEndTick, sSent, sKilled, sPlantsEaten, sBrainsEaten, BRAINS_TO_WIN);
		}

		// PVZ_MOD_TEST_VERSUS_DEFENSE=debil|normal|fuerte: how much the test plants. A weak one lets the zombies
		// win, a strong one holds; the normal one loses a brain or two and should last the whole match.
		int TestDefense()
		{
			static const int sDefense = [] {
				const char* aDefense = SDL_getenv("PVZ_MOD_TEST_VERSUS_DEFENSE");
				if (aDefense && SDL_strcmp(aDefense, "debil") == 0)
					return 0;
				if (aDefense && SDL_strcmp(aDefense, "fuerte") == 0)
					return 2;
				return 1;
			}();
			return sDefense;
		}

		// Plants theSeedType at the tile, with a lily pad or a pot under it where the tile needs one.
		void TestPlant(Board* theBoard, int theCol, int theRow, SeedType theSeedType)
		{
			if (theBoard->CanPlantAt(theCol, theRow, theSeedType) != PlantingReason::PLANTING_OK)
			{
				SeedType aBase = theBoard->IsPoolSquare(theCol, theRow) ? SeedType::SEED_LILYPAD : theBoard->StageHasRoof() ? SeedType::SEED_FLOWERPOT : SeedType::SEED_NONE;
				if (aBase != SeedType::SEED_NONE && theBoard->CanPlantAt(theCol, theRow, aBase) == PlantingReason::PLANTING_OK)
					theBoard->AddPlant(theCol, theRow, aBase);
			}
			if (theBoard->CanPlantAt(theCol, theRow, theSeedType) == PlantingReason::PLANTING_OK)
				theBoard->AddPlant(theCol, theRow, theSeedType);
		}

		void TestPlantDefense(Board* theBoard)
		{
			bool aNight = theBoard->StageIsNight();
			bool aRoof = theBoard->StageHasRoof();
			int aDefense = TestDefense();
			SeedType aSun = aNight ? SeedType::SEED_SUNSHROOM : SeedType::SEED_SUNFLOWER;
			SeedType aShooter = aRoof ? SeedType::SEED_CABBAGEPULT : aNight ? SeedType::SEED_FUMESHROOM : SeedType::SEED_PEASHOOTER;
			SeedType aStrong = aRoof ? SeedType::SEED_MELONPULT : aNight ? SeedType::SEED_GLOOMSHROOM : SeedType::SEED_REPEATER;
			for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
			{
				if (!IsLane(theBoard, aRow))
					continue;
				TestPlant(theBoard, 0, aRow, aSun);
				if (aDefense == 0)
				{
					if (aRow % 2 == 0)
						TestPlant(theBoard, 1, aRow, aShooter);
					continue;
				}
				TestPlant(theBoard, 1, aRow, aShooter);
				TestPlant(theBoard, 2, aRow, aDefense == 2 ? aStrong : aShooter);
				TestPlant(theBoard, 3, aRow, aRoof || aDefense == 2 ? SeedType::SEED_TALLNUT : SeedType::SEED_WALLNUT);
				if (aDefense == 2)
				{
					TestPlant(theBoard, 4, aRow, aRoof ? aStrong : SeedType::SEED_SNOWPEA);
					TestPlant(theBoard, 5, aRow, SeedType::SEED_SPIKEWEED);
					if (aNight)
						TestPlant(theBoard, 1, aRow, SeedType::SEED_PLANTERN);
				}
			}
			// Something against balloons and the fog
			if (aDefense > 0)
			{
				TestPlant(theBoard, 4, 0, SeedType::SEED_CACTUS);
				TestPlant(theBoard, 4, std::min(4, MAX_GRID_SIZE_Y - 1), SeedType::SEED_CACTUS);
			}
			if (theBoard->StageHasFog())
				TestPlant(theBoard, 2, 2, SeedType::SEED_PLANTERN);
			int aPlants = 0;
			for (Plant* aPlant : theBoard->mPlants)
				aPlants += !aPlant->mDead;
			PvzpLogLn("Versus test: defense {} planted, {} plants", aDefense, aPlants);
		}

		// The test plants a defense and plays the zombie side through the key handler, so a muted test can
		// watch a whole match (PVZ_MOD_TEST=mode6..mode10 in Mods.cpp): it goes through the roster in order, page
		// by page with Q/E and 1-9, saving up for each zombie and moving to a lane it can use; at night every
		// fifth zombie that can rise comes out of a grave. Locked zombies and those with no lane are skipped.
		void UpdateTestPlayer(Board* theBoard)
		{
			if (!IsTestPlayer())
				return;

			static int sNext = 0;
			static int sWaited = 0;
			static int sSentCount = 0;
			static bool sPageShot[MAX_PAGES] = {};
			int aTick = theBoard->mMainCounter;
			if (aTick == 5)
			{
				sNext = sWaited = sSentCount = 0;
				std::fill(sPageShot, sPageShot + MAX_PAGES, false);
				TestPlantDefense(theBoard);
			}
			if (aTick < GraceTicks() || aTick % 50 != 0)
				return;

			// Zombies not sent yet go first, so every page gets used
			static bool sUsed[MAX_ROSTER] = {};
			if (aTick == GraceTicks())
				std::fill(sUsed, sUsed + MAX_ROSTER, false);
			for (int i = 0; i < sNumRoster; i++)
			{
				int aIndex = (sNext + i) % sNumRoster;
				if (!sUsed[aIndex] && sPhase >= sRoster[aIndex].mEntry->mPhase && i > 0 && sUsed[sNext % sNumRoster])
				{
					sNext = aIndex;
					break;
				}
			}

			for (int aSkips = 0; aSkips < sNumRoster; aSkips++)
			{
				int aIndex = sNext % sNumRoster;
				const Slot& aSlot = sRoster[aIndex];
				ZombieType aType = aSlot.mEntry->mZombieType;
				bool aGrave = StageHasGraves(theBoard) && CanRiseFromGrave(aType) && sSentCount % 5 == 4;
				int aLane = -1;
				for (int i = 1; i <= MAX_GRID_SIZE_Y && aLane < 0; i++)
				{
					int aRow = (sLane + i) % MAX_GRID_SIZE_Y;
					if (aGrave ? GraveInLane(theBoard, aRow) != nullptr : CanGoInLane(theBoard, aType, aRow))
						aLane = aRow;
				}
				// A bobsled waits a little for the ice of a Zomboni
				bool aWaitForIce = aType == ZombieType::ZOMBIE_BOBSLED && aLane < 0 && sWaited <= 20;
				if (aWaitForIce)
				{
					sWaited++;
					return;
				}
				if (sPhase < aSlot.mEntry->mPhase || aLane < 0 || sWaited > 40)
				{
					sNext++;
					sWaited = 0;
					continue;
				}
				if (aGrave != sGraveMode)
					HandleKey(theBoard, 'T');
				if (aTick < aSlot.mReadyTick || sBrains < Cost(aIndex) || (aGrave && aTick < sGraveReady))
				{
					sWaited++;
					return;		// saving up for it
				}
				for (int i = 0; i < MAX_GRID_SIZE_Y && sLane != aLane; i++)
					HandleKey(theBoard, sLane < aLane ? 'S' : 'W');
				for (int i = 0; i < MAX_PAGES && sPage != aSlot.mPage; i++)
					HandleKey(theBoard, 'E');
				int aBrains = sBrains;
				int aSent = sSent;
				HandleKey(theBoard, '1' + aIndex - sPageStart[sPage]);
				PvzpLogLn("Versus test: tick {} phase {} lane {} page {} key {} ({}{}) brains {} -> {}{}", aTick, sPhase + 1, sLane, sPage + 1, aIndex - sPageStart[sPage] + 1,
					aSlot.mEntry->mName, sGraveMode ? ", tumba" : "", aBrains, sBrains, sSent > aSent ? "" : " NOT SENT");
#ifdef PVZ_HACK_MENU
				const char* aShots = SDL_getenv("PVZ_MOD_TEST_SHOTS");
				if (aShots && !sPageShot[sPage])
				{
					sPageShot[sPage] = true;
					HackMenu::RequestScreenshot(std::format("{}/versus-{}-page{}.png", aShots, sStage->mName, sPage + 1).c_str());
				}
#endif
				sUsed[aIndex] = sSent > aSent;
				sSentCount++;
				sNext++;
				sWaited = 0;
				return;
			}
		}

		// The zombie portraits of the bar, laid out like the I, Zombie seed packets (DrawSeedPacket)
		void PortraitOffsets(ZombieType theZombieType, float& theScale, float& theX, float& theY)
		{
			theScale = 0.35f;
			theX = -3.0f;
			theY = -6.0f;
			switch (theZombieType)
			{
			case ZombieType::ZOMBIE_POLEVAULTER:		theX = -8.0f; theY = -11.0f;	break;
			case ZombieType::ZOMBIE_LADDER:
			case ZombieType::ZOMBIE_DIGGER:
			case ZombieType::ZOMBIE_DOOR:
			case ZombieType::ZOMBIE_POGO:				theY = -9.0f;					break;
			case ZombieType::ZOMBIE_BUNGEE:				theScale = 0.3f; theX = 1.0f; theY = 0.0f;		break;
			case ZombieType::ZOMBIE_FOOTBALL:			theScale = 0.33f; theX = -7.0f; theY = -8.0f;	break;
			case ZombieType::ZOMBIE_BALLOON:			theY = -4.0f;					break;
			case ZombieType::ZOMBIE_IMP:				theScale = 0.4f; theX = -12.0f; theY = -16.0f;	break;
			case ZombieType::ZOMBIE_ZAMBONI:			theScale = 0.23f; theX = 12.0f; theY = 4.0f;	break;
			case ZombieType::ZOMBIE_GARGANTUAR:
			case ZombieType::ZOMBIE_REDEYE_GARGANTUAR:	theScale = 0.23f; theX = 4.0f; theY = 4.0f;	break;
			case ZombieType::ZOMBIE_DANCER:				theScale = 0.3f; theX = 1.0f; theY = 3.0f;		break;
			case ZombieType::ZOMBIE_DOLPHIN_RIDER:		theScale = 0.3f; theX = -6.0f; theY = -6.0f;	break;
			case ZombieType::ZOMBIE_BOBSLED:			theScale = 0.25f; theX = 4.0f; theY = 6.0f;		break;
			case ZombieType::ZOMBIE_CATAPULT:			theScale = 0.23f; theX = 8.0f; theY = 6.0f;		break;
			case ZombieType::ZOMBIE_GIANT_PAIL:			theScale = 0.4f; theX = -9.0f; theY = -12.0f;	break;
			default:																	break;
			}
		}

		// The mod zombies' colors (Zombie::DrawReanim), as a tint on the portrait
		Color PortraitTint(ZombieType theZombieType)
		{
			switch (theZombieType)
			{
			case ZombieType::ZOMBIE_SPEEDY:		return Color(255, 140, 140);
			case ZombieType::ZOMBIE_ICE:		return Color(170, 225, 255);
			case ZombieType::ZOMBIE_GHOST:		return Color(230, 240, 255, ModContent::GHOST_ALPHA + 40);
			case ZombieType::ZOMBIE_HEALER:		return Color(170, 255, 170);
			case ZombieType::ZOMBIE_KAMIKAZE:	return Color(255, 190, 110);
			case ZombieType::ZOMBIE_SPLITTER:	return Color(255, 240, 140);
			case ZombieType::ZOMBIE_HOPPER:		return Color(255, 170, 255);
			default:							return Color(255, 255, 255);
			}
		}

		SeedType ZombotanyHead(ZombieType theZombieType)
		{
			switch (theZombieType)
			{
			case ZombieType::ZOMBIE_PEA_HEAD:		return SeedType::SEED_PEASHOOTER;
			case ZombieType::ZOMBIE_WALLNUT_HEAD:	return SeedType::SEED_WALLNUT;
			case ZombieType::ZOMBIE_JALAPENO_HEAD:	return SeedType::SEED_JALAPENO;
			case ZombieType::ZOMBIE_GATLING_HEAD:	return SeedType::SEED_GATLINGPEA;
			case ZombieType::ZOMBIE_SQUASH_HEAD:	return SeedType::SEED_SQUASH;
			case ZombieType::ZOMBIE_TALLNUT_HEAD:	return SeedType::SEED_TALLNUT;
			default:								return SeedType::SEED_NONE;
			}
		}

		// A seed packet with the zombie on it; thePercentDark shades it from the top while it recharges.
		void DrawZombiePacket(Graphics* g, float theX, float theY, float theScale, ZombieType theZombieType, float thePercentDark, int theGray)
		{
			int aGray = theGray;
			g->SetColorizeImages(true);
			g->SetColor(Color(aGray, aGray, aGray));
			PvzpDrawImageCelScaledF(g, Sexy::IMAGE_SEEDS, theX, theY, 2, 0, theScale, theScale);

			float aScale, aOffsetX, aOffsetY;
			PortraitOffsets(theZombieType, aScale, aOffsetX, aOffsetY);
			Graphics aZombieG(*g);
			aZombieG.ClipRect(static_cast<int>(theX + 3 * theScale), static_cast<int>(theY + 3 * theScale), static_cast<int>(44 * theScale), static_cast<int>(50 * theScale));
			aZombieG.mScaleX = aZombieG.mScaleY = aScale * theScale;
			Color aTint = PortraitTint(theZombieType);
			aZombieG.SetColorizeImages(true);
			aZombieG.SetColor(Color(aTint.mRed * aGray / 255, aTint.mGreen * aGray / 255, aTint.mBlue * aGray / 255, aTint.mAlpha));
			ZombieType aDrawType = theZombieType == ZombieType::ZOMBIE_GIANT_PAIL ? ZombieType::ZOMBIE_PAIL : theZombieType;
			gLawnApp->mReanimatorCache->DrawCachedZombie(&aZombieG, theX + aOffsetX * theScale, theY + aOffsetY * theScale, aDrawType);
			SeedType aHead = ZombotanyHead(theZombieType);
			if (aHead != SeedType::SEED_NONE)
			{
				aZombieG.mScaleX = aZombieG.mScaleY = 0.28f * theScale;
				aZombieG.SetColor(Color(aGray, aGray, aGray));
				gLawnApp->mReanimatorCache->DrawCachedPlant(&aZombieG, theX + 6 * theScale, theY + 2 * theScale, aHead, DrawVariation::VARIATION_NORMAL);
			}

			if (thePercentDark > 0.0f)
			{
				int aDarkHeight = static_cast<int>(70 * theScale * thePercentDark);
				g->SetColorizeImages(false);
				g->SetColor(Color(0, 0, 0, 140));
				g->FillRect(static_cast<int>(theX), static_cast<int>(theY), static_cast<int>(50 * theScale), aDarkHeight);
			}
			g->SetColorizeImages(false);
		}

		void DrawTextShadow(Graphics* g, const std::string& theText, int theX, int theY, _Font* theFont, const Color& theColor, DrawStringJustification theJustification)
		{
			PvzpDrawString(g, theText, theX + 1, theY + 1, theFont, Color(0, 0, 0, theColor.mAlpha), theJustification);
			PvzpDrawString(g, theText, theX, theY, theFont, theColor, theJustification);
		}

		// Small text squeezed to fit theWidth, centered on theX, with a shadow (the zombie names on the packets)
		void DrawFittedText(Graphics* g, const std::string& theText, float theX, float theY, float theWidth, _Font* theFont, const Color& theColor)
		{
			float aWidth = static_cast<float>(theFont->StringWidth(theText));
			float aScale = aWidth > theWidth ? theWidth / aWidth : 1.0f;
			for (int aShadow = 1; aShadow >= 0; aShadow--)
			{
				SexyMatrix3 aMatrix;
				PvzpScaleTransformMatrix(aMatrix, theX - aWidth * aScale / 2 + aShadow, theY + aShadow, aScale, 1.0f);
				PvzpDrawStringMatrix(g, theFont, aMatrix, theText, aShadow ? Color(0, 0, 0, theColor.mAlpha) : theColor);
			}
		}

		void DrawBrainIcon(Graphics* g, float theX, float theY, float theScale, bool theEaten)
		{
			if (theEaten)
			{
				g->SetColorizeImages(true);
				g->SetColor(Color(90, 70, 70));
				PvzpDrawImageScaledF(g, Sexy::IMAGE_BRAIN, theX, theY + 20 * theScale, theScale, theScale * 0.35f);
				g->SetColorizeImages(false);
				return;
			}
			PvzpDrawImageScaledF(g, Sexy::IMAGE_BRAIN, theX, theY, theScale, theScale);
		}

		constexpr int BAR_TOP = 541;
		constexpr int PACKET_LEFT = 126;
		constexpr int PACKET_STEP = 41;
		constexpr float PACKET_SCALE = 0.76f;

		void DrawBar(Board* theBoard, Graphics* g, bool theOver)
		{
			int aTick = theBoard->mMainCounter;
			g->SetColor(Color(0, 0, 0, 165));
			g->FillRect(4, BAR_TOP, 592, 600 - BAR_TOP);
			g->FillRect(600, BAR_TOP, 196, 600 - BAR_TOP);

			// Brains and income
			DrawBrainIcon(g, 9, BAR_TOP + 4, 0.8f, false);
			DrawTextShadow(g, std::to_string(sBrains), 40, BAR_TOP + 24, Sexy::FONT_HOUSEOFTERROR20, Color(255, 130, 220), DrawStringJustification::DS_ALIGN_LEFT);
			DrawTextShadow(g, std::format("+{} cada 3 s", BRAIN_INCOME[sPhase]), 10, BAR_TOP + 40, Sexy::FONT_BRIANNETOD12, Color(220, 180, 220), DrawStringJustification::DS_ALIGN_LEFT);
			std::string aLaneText = std::format("Carril {}", sLane + 1);
			if (theBoard->mPlantRow[sLane] == PlantRowType::PLANTROW_POOL)
				aLaneText += " (agua)";
			if (sGraveMode)
				aLaneText = std::format("Tumba, carril {}", sLane + 1);
			DrawTextShadow(g, aLaneText, 10, BAR_TOP + 55, Sexy::FONT_BRIANNETOD12, sGraveMode ? Color(200, 150, 255) : Color(255, 160, 160), DrawStringJustification::DS_ALIGN_LEFT);

			// The page's packets
			int aCount = PageCount(sPage);
			for (int i = 0; i < aCount; i++)
			{
				int aIndex = sPageStart[sPage] + i;
				const Slot& aSlot = sRoster[aIndex];
				SendCheck aCheck = theOver ? SendCheck::OVER : CanSend(theBoard, aIndex);
				float aDark = aTick < aSlot.mReadyTick ? std::min(1.0f, static_cast<float>(aSlot.mReadyTick - aTick) / aSlot.mEntry->mRecharge) : 0.0f;
				// Like the seed packets: a bit darker without the brains for it, much darker when it can't go at all
				int aGray = aCheck == SendCheck::OK || aCheck == SendCheck::RECHARGING || aCheck == SendCheck::GRACE ? 255 :
					aCheck == SendCheck::BRAINS || aCheck == SendCheck::GRAVE_RECHARGING ? 175 : 95;
				int aX = PACKET_LEFT + i * PACKET_STEP;
				int aY = BAR_TOP + 3;
				DrawZombiePacket(g, aX, aY, PACKET_SCALE, aSlot.mEntry->mZombieType, aDark, aGray);

				int aCost = Cost(aIndex);
				Color aCostColor = sBrains >= aCost ? Color(0, 0, 0) : Color(190, 0, 0);
				PvzpDrawString(g, std::to_string(aCost), aX + 34, aY + 50, Sexy::FONT_PICO129, aCostColor, DrawStringJustification::DS_ALIGN_RIGHT);
				DrawTextShadow(g, std::to_string(i + 1), aX + 3, aY + 11, Sexy::FONT_BRIANNETOD12, Color(255, 255, 255), DrawStringJustification::DS_ALIGN_LEFT);
				g->SetColor(Color(0, 0, 0, 120));
				g->FillRect(aX + 2, aY + 35, 34, 9);
				DrawFittedText(g, aSlot.mEntry->mName, aX + 19, aY + 43, 34, Sexy::FONT_PICO129, aGray == 255 ? Color(255, 255, 255) : Color(190, 190, 190));
				if (aCheck == SendCheck::LOCKED)
					DrawTextShadow(g, std::format("F{}", aSlot.mEntry->mPhase + 1), aX + 19, aY + 30, Sexy::FONT_HOUSEOFTERROR16, Color(255, 200, 80), DrawStringJustification::DS_ALIGN_CENTER);
				else if (aCheck == SendCheck::LANE || aCheck == SendCheck::NO_GRAVE)
					DrawTextShadow(g, "X", aX + 19, aY + 30, Sexy::FONT_HOUSEOFTERROR16, Color(255, 70, 70), DrawStringJustification::DS_ALIGN_CENTER);
			}

			// Page: name, Q/E and the name of each zombie on it
			int aPageX = PACKET_LEFT + aCount * PACKET_STEP + 6;
			int aPageRight = 594;
			DrawTextShadow(g, sStage->mPages[sPage].mName, (aPageX + aPageRight) / 2, BAR_TOP + 17, Sexy::FONT_BRIANNETOD16, Color(255, 230, 150), DrawStringJustification::DS_ALIGN_CENTER);
			DrawTextShadow(g, std::format("Q < {}/{} > E", sPage + 1, sNumPages), (aPageX + aPageRight) / 2, BAR_TOP + 33, Sexy::FONT_BRIANNETOD12, Color(220, 220, 220), DrawStringJustification::DS_ALIGN_CENTER);
			if (StageHasGraves(theBoard))
				DrawTextShadow(g, sGraveMode ? "T: calle" : "T: tumba", (aPageX + aPageRight) / 2, BAR_TOP + 49, Sexy::FONT_BRIANNETOD12, Color(200, 150, 255), DrawStringJustification::DS_ALIGN_CENTER);

			// Phase, clock, the match bar and the brains eaten
			int aLeft = DurationTicks() - (theOver && sEndTick > 0 ? sEndTick : aTick);
			std::string aPhaseText = aTick < GraceTicks() ? "Preparacion" : std::format("Fase {}: {}", sPhase + 1, PHASE_NAMES[sPhase]);
			DrawTextShadow(g, aPhaseText, 605, BAR_TOP + 15, Sexy::FONT_BRIANNETOD12, Color(255, 230, 150), DrawStringJustification::DS_ALIGN_LEFT);
			std::string aClock = aTick < GraceTicks() ? std::format("Zombis en {} s", (GraceTicks() - aTick + 99) / 100) : ClockText(aLeft);
			DrawTextShadow(g, aClock, 791, BAR_TOP + 15, Sexy::FONT_BRIANNETOD12, aLeft < 3000 && aTick >= GraceTicks() ? Color(255, 120, 120) : Color(255, 255, 255), DrawStringJustification::DS_ALIGN_RIGHT);

			int aBarX = 605, aBarY = BAR_TOP + 21, aBarW = 186, aBarH = 7;
			g->SetColor(Color(60, 50, 40, 220));
			g->FillRect(aBarX, aBarY, aBarW, aBarH);
			int aDone = std::clamp(aBarW * std::min(theOver && sEndTick > 0 ? sEndTick : aTick, DurationTicks()) / DurationTicks(), 0, aBarW);
			g->SetColor(Color(110, 200, 90));
			g->FillRect(aBarX, aBarY, aDone, aBarH);
			for (int aPhase = 1; aPhase < NUM_PHASES; aPhase++)
			{
				int aMarkX = aBarX + aBarW * PHASE_START_PERCENT[aPhase] / 100;
				g->SetColor(sPhase >= aPhase ? Color(255, 80, 80) : Color(230, 230, 230));
				g->FillRect(aMarkX - 1, aBarY - 3, 3, aBarH + 6);
			}

			DrawTextShadow(g, "Cerebros comidos", 605, BAR_TOP + 46, Sexy::FONT_BRIANNETOD12, Color(230, 230, 230), DrawStringJustification::DS_ALIGN_LEFT);
			for (int i = 0; i < BRAINS_TO_WIN; i++)
				DrawBrainIcon(g, 718 + i * 25, BAR_TOP + 32, 0.7f, i >= BRAINS_TO_WIN - sBrainsEaten);
		}

		void DrawSummary(Board* theBoard, Graphics* g)
		{
			bool aPlants = sWinner == Winner::PLANTS;
			int aX = 170, aY = 92, aW = 460, aH = 214;
			g->SetColor(Color(0, 0, 0, 190));
			g->FillRect(aX, aY, aW, aH);
			g->SetColor(aPlants ? Color(120, 230, 90) : Color(255, 90, 160));
			g->DrawRect(aX, aY, aW - 1, aH - 1);
			g->DrawRect(aX + 2, aY + 2, aW - 5, aH - 5);

			DrawTextShadow(g, aPlants ? "GANAN LAS PLANTAS!" : "GANAN LOS ZOMBIS!", 400, aY + 44, Sexy::FONT_HOUSEOFTERROR28, aPlants ? Color(150, 255, 110) : Color(255, 110, 190), DrawStringJustification::DS_ALIGN_CENTER);
			std::string aWhy = aPlants ? std::format("Aguantaron hasta el final con {} de {} cerebros", BRAINS_TO_WIN - sBrainsEaten, BRAINS_TO_WIN) : std::format("Se comieron {} cerebros de la casa", sBrainsEaten);
			DrawTextShadow(g, aWhy, 400, aY + 70, Sexy::FONT_BRIANNETOD16, Color(255, 255, 255), DrawStringJustification::DS_ALIGN_CENTER);

			std::string aLines[6] = {
				std::format("Zombis mandados: {}", sSent),
				std::format("Zombis matados: {}", sKilled),
				std::format("Plantas comidas: {}", sPlantsEaten),
				std::format("Cerebros comidos: {}/{}", sBrainsEaten, BRAINS_TO_WIN),
				std::format("Tiempo: {}", ClockText(sEndTick)),
				std::format("Fase: {}", PHASE_NAMES[sPhase]),
			};
			for (int i = 0; i < 6; i++)
				DrawTextShadow(g, aLines[i], i % 2 == 0 ? aX + 40 : aX + 250, aY + 106 + (i / 2) * 26, Sexy::FONT_BRIANNETOD16, Color(255, 230, 150), DrawStringJustification::DS_ALIGN_LEFT);
			std::string aFooter = aPlants ? "Recoge el trofeo para terminar" : "";
			if (!aFooter.empty())
				DrawTextShadow(g, aFooter, 400, aY + aH - 14, Sexy::FONT_BRIANNETOD12, Color(200, 200, 200), DrawStringJustification::DS_ALIGN_CENTER);
		}
	}

	const RosterEntry& GetRosterEntry(int theIndex)
	{
		return *sRoster[std::clamp(theIndex, 0, std::max(0, sNumRoster - 1))].mEntry;
	}

	int GetRosterSize()
	{
		return sNumRoster;
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

	void AllowZombies(GameMode theGameMode, bool* theAllowed)
	{
		const StageDef& aStage = StageFor(theGameMode);
		for (int aPage = 0; aPage < aStage.mNumPages; aPage++)
		{
			for (int i = 0; i < aStage.mPages[aPage].mCount; i++)
			{
				ZombieType aType = aStage.mPages[aPage].mTypes[i];
				theAllowed[aType] = true;
				if (aType == ZombieType::ZOMBIE_DANCER)
					theAllowed[ZombieType::ZOMBIE_BACKUP_DANCER] = true;
				else if (aType == ZombieType::ZOMBIE_GARGANTUAR || aType == ZombieType::ZOMBIE_REDEYE_GARGANTUAR)
					theAllowed[ZombieType::ZOMBIE_IMP] = true;
				else if (aType == ZombieType::ZOMBIE_SPLITTER)
					theAllowed[ZombieType::ZOMBIE_MINI] = true;
			}
		}
	}

	void StartLevel(Board* theBoard)
	{
		if (!IsVersus(theBoard->mApp->mGameMode))
			return;

		BuildRoster(theBoard->mApp->mGameMode);
		sBrains = START_BRAINS;
		sLane = 2;
		sPage = 0;
		sPhase = 0;
		sGraveMode = false;
		sGraveReady = 0;
		sWinner = Winner::NONE;
		sEndTick = 0;
		sGameOverShown = false;
		sWarnedEnd = false;
		sSent = sKilled = sPlantsEaten = sBrainsEaten = sBrainBonus = sSunBonus = 0;
		sMessage.clear();
		sMessageTick = -MESSAGE_TICKS;
		sNumFloatTexts = 0;
		sNumGraves = 0;
		if (!IsLane(theBoard, sLane))
			MoveLane(theBoard, 1);

		theBoard->mSunMoney = sStage->mStartSun;
		PlaceBrains(theBoard);
		for (int i = 0; i < sNumRoster; i++)
			Zombie::PreloadZombieResources(sRoster[i].mEntry->mZombieType);
		UpdateGraves(theBoard);

		// The rules in the middle of the lawn while the plants get going; the address for the other device stays above the bar
		std::string aAdvice = std::format("Versus {}. Zombis: W/S carril, Q/E pagina, 1-9 mandar. Ganan si se comen {} cerebros; las plantas, si aguantan {}",
			sStage->mName, BRAINS_TO_WIN, ClockText(DurationTicks()));
		theBoard->DisplayAdvice(aAdvice, MessageStyle::MESSAGE_STYLE_BIG_MIDDLE, AdviceType::ADVICE_NONE);
		bool aCheats = false;
#ifdef PVZ_HACK_MENU
		aCheats = Mods::MatchCheatsOn();
#endif
		if (aCheats)
			ShowMessage(theBoard, "Trucos en pausa en Versus (solo cuenta la velocidad)", GRACE_TICKS);
		PvzpLogLn("Versus: {} starts, {} zombies in {} pages, {} s, cheats paused {}", sStage->mName, sNumRoster, sNumPages, DurationTicks() / 100, aCheats);
	}

	void Update(Board* theBoard)
	{
		if (!IsVersus(theBoard->mApp->mGameMode))
			return;

		int aTick = theBoard->mMainCounter;
		if (sWinner == Winner::ZOMBIES)
		{
			// The lawn shows who won for a moment, then the game-over dialog with the summary (Board::ZombiesWon)
			if (!sGameOverShown && aTick - sEndTick >= WIN_PAUSE_TICKS)
			{
				sGameOverShown = true;
				theBoard->ZombiesWon(nullptr);
			}
			return;
		}
		if (IsMatchOver(theBoard))
			return;

		int aPhase = PhaseAt(aTick);
		if (aPhase > sPhase)
		{
			sPhase = aPhase;
			AnnouncePhase(theBoard);
		}
		if (aTick > 0 && aTick % BRAIN_INTERVAL == 0)
			sBrains += BRAIN_INCOME[sPhase];
		if (!sWarnedEnd && DurationTicks() - aTick <= 3000 && DurationTicks() > 6000)
		{
			sWarnedEnd = true;
			theBoard->DisplayAdvice("Ultimos 30 segundos!", MessageStyle::MESSAGE_STYLE_BIG_MIDDLE_FAST, AdviceType::ADVICE_NONE);
		}
		if (aTick % 10 == 0)
			UpdateGraves(theBoard);

		int aKey = 0;
		while (sNet.mPopKey && sNet.mPopKey(&aKey))
		{
			int aBrains = sBrains;
			HandleKey(theBoard, aKey);
			PvzpLogLn("Versus: remote key {} at tick {}: lane {} page {} brains {} -> {}", aKey, aTick, sLane, sPage + 1, aBrains, sBrains);
		}

		UpdateTestPlayer(theBoard);

		if (aTick >= DurationTicks() && sWinner == Winner::NONE)
			PlantsWin(theBoard);
	}

	SendCheck CanSend(Board* theBoard, int theIndex)
	{
		if (theIndex < 0 || theIndex >= sNumRoster || IsMatchOver(theBoard))
			return SendCheck::OVER;
		const Slot& aSlot = sRoster[theIndex];
		ZombieType aType = aSlot.mEntry->mZombieType;
		int aTick = theBoard->mMainCounter;
		if (aTick < GraceTicks())
			return SendCheck::GRACE;
		if (sPhase < aSlot.mEntry->mPhase)
			return SendCheck::LOCKED;
		if (sGraveMode)
		{
			if (!CanRiseFromGrave(aType) || !GraveInLane(theBoard, sLane))
				return SendCheck::NO_GRAVE;
		}
		else if (!CanGoInLane(theBoard, aType, sLane))
			return SendCheck::LANE;
		if (aTick < aSlot.mReadyTick)
			return SendCheck::RECHARGING;
		if (sGraveMode && aTick < sGraveReady)
			return SendCheck::GRAVE_RECHARGING;
		if (sBrains < Cost(theIndex))
			return SendCheck::BRAINS;
		return SendCheck::OK;
	}

	bool SendZombie(Board* theBoard, int theIndex)
	{
		if (CanSend(theBoard, theIndex) != SendCheck::OK)
			return false;

		Slot& aSlot = sRoster[theIndex];
		ZombieType aType = aSlot.mEntry->mZombieType;
		int aTick = theBoard->mMainCounter;
		int aCost = Cost(theIndex);
		int aSent = 1;
		Zombie* aZombie = theBoard->AddZombieInRow(aType, sLane, theBoard->mCurrentWave);
		if (aZombie == nullptr || aZombie->mDead)
			return false;

		if (sGraveMode)
		{
			GridItem* aGrave = GraveInLane(theBoard, sLane);
			aZombie->RiseFromGrave(aGrave->mGridX, aGrave->mGridY);
			sGraveReady = aTick + GRAVE_RECHARGE;
		}
		else if (aType == ZombieType::ZOMBIE_BUNGEE)
			AimBungee(theBoard, aZombie, sLane);
		else if (aType == ZombieType::ZOMBIE_FLAG)
		{
			// The flag brings a zombie in every other lane
			for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
			{
				ZombieType aFollower = aRow % 2 == 0 ? ZombieType::ZOMBIE_TRAFFIC_CONE : ZombieType::ZOMBIE_NORMAL;
				if (aRow != sLane && CanGoInLane(theBoard, aFollower, aRow) && theBoard->AddZombieInRow(aFollower, aRow, theBoard->mCurrentWave))
					aSent++;
			}
			theBoard->mApp->PlaySample(Sexy::SOUND_HUGE_WAVE);
			ShowMessage(theBoard, "Oleada! Un zombi en cada carril");
		}

		sBrains -= aCost;
		aSlot.mReadyTick = aTick + aSlot.mEntry->mRecharge;
		sSent += aSent;
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

	bool ZombieReachedHouse(Zombie* theZombie)
	{
		Board* aBoard = theZombie->mBoard;
		if (!aBoard || !IsVersus(aBoard->mApp->mGameMode))
			return false;

		if (sWinner == Winner::NONE)
		{
			if (GridItem* aBrain = BrainFor(aBoard, theZombie->mRow))
			{
				aBrain->mGridItemState = GridItemState::GRIDITEM_STATE_BRAIN_SQUISHED;
				aBrain->mGridItemCounter = 1000000;		// stays squished for the rest of the match
				aBrain->mRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_GRAVE_STONE, aBrain->mGridY, 0);
				sBrainsEaten++;
				aBoard->mApp->PlayFoley(FoleyType::FOLEY_SQUISH);
				aBoard->mApp->PlayFoley(FoleyType::FOLEY_BRAINS);
				aBoard->mApp->AddPvzpParticle(aBrain->mPosX + 16, aBrain->mPosY + 16, Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_TOP, 0, 0), ParticleEffect::PARTICLE_POWIE);
				aBoard->ShakeBoard(3, -4);
				AddFloatText(aBoard, static_cast<int>(aBrain->mPosX) + 60, static_cast<int>(aBrain->mPosY), Color(255, 110, 190), std::format("Cerebro comido! {}/{}", sBrainsEaten, BRAINS_TO_WIN));
				ShowMessage(aBoard, std::format("Los zombis se comieron un cerebro: {} de {}", sBrainsEaten, BRAINS_TO_WIN));
				PvzpLogLn("Versus: zombie type {} ate the brain of lane {} (came in lane {}) at tick {}: {}/{}", static_cast<int>(theZombie->mZombieType), aBrain->mGridY, theZombie->mRow, aBoard->mMainCounter, sBrainsEaten, BRAINS_TO_WIN);
			}
		}
		theZombie->DieNoLoot();
		if (sWinner == Winner::NONE && sBrainsEaten >= BRAINS_TO_WIN)
			ZombiesWin(aBoard);
		return true;
	}

	void OnZombieKilled(Zombie* theZombie)
	{
		Board* aBoard = theZombie->mBoard;
		if (!aBoard || !IsVersus(aBoard->mApp->mGameMode) || sWinner != Winner::NONE || theZombie->mMindControlled)
			return;
		sKilled++;
		const RosterEntry* aEntry = FindEntry(theZombie->mZombieType);
		if (!aEntry || aEntry->mCost < KILL_REWARD_MIN_COST)
			return;
		int aSuns = std::max(1, aEntry->mCost * KILL_REWARD_PERCENT / 100 / 25);
		for (int i = 0; i < aSuns; i++)
			aBoard->AddCoin(theZombie->mX + 30 + i * 15, theZombie->mY + 40, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_COIN);
		sSunBonus += aSuns * 25;
		AddFloatText(aBoard, theZombie->mX + 60, theZombie->mY + 30, Color(255, 240, 80), std::format("+{} sol", aSuns * 25));
	}

	void OnPlantEaten(Plant* thePlant)
	{
		Board* aBoard = thePlant->mBoard;
		if (!aBoard || !IsVersus(aBoard->mApp->mGameMode) || sWinner != Winner::NONE)
			return;
		sPlantsEaten++;
		int aBonus = std::clamp(Plant::GetCost(thePlant->mSeedType, thePlant->mImitaterType) * PLANT_EATEN_BONUS_PERCENT / 100 / 5 * 5, PLANT_EATEN_BONUS_MIN, PLANT_EATEN_BONUS_MAX);
		sBrains += aBonus;
		sBrainBonus += aBonus;
		AddFloatText(aBoard, thePlant->mX + 40, thePlant->mY + 20, Color(255, 130, 220), std::format("+{} cerebros", aBonus));
	}

	std::string ZombiesWonMessage()
	{
		return std::format("Ganan los zombis: se comieron {} cerebros de la casa.\n\nZombis mandados: {}\nZombis matados: {}\nPlantas comidas: {}\nTiempo: {}",
			sBrainsEaten, sSent, sKilled, sPlantsEaten, ClockText(sEndTick));
	}

	std::string StateJson(Board* theBoard)
	{
		if (!theBoard || !IsVersus(theBoard->mApp->mGameMode) || sNumRoster == 0)
			return "{\"playing\":false}";

		int aTick = theBoard->mMainCounter;
		bool aOver = IsMatchOver(theBoard);
		int aEnd = sWinner != Winner::NONE ? sEndTick : aTick;
		std::string aJson = std::format("{{\"playing\":true,\"stage\":\"{}\",\"phase\":{},\"phaseName\":\"{}\",\"grace\":{},\"left\":{},\"clock\":\"{}\","
			"\"brains\":{},\"income\":{},\"eaten\":{},\"need\":{},\"lane\":{},\"page\":{},\"graves\":{},\"graveMode\":{},\"over\":{},\"winner\":\"{}\","
			"\"stats\":{{\"sent\":{},\"killed\":{},\"plantsEaten\":{},\"brainsEaten\":{},\"time\":\"{}\"}},\"message\":\"{}\",",
			sStage->mName, sPhase + 1, PHASE_NAMES[sPhase], std::max(0, (GraceTicks() - aTick + 99) / 100), std::max(0, DurationTicks() - aEnd) / 100, ClockText(DurationTicks() - aEnd),
			sBrains, BRAIN_INCOME[sPhase], sBrainsEaten, BRAINS_TO_WIN, sLane, sPage, StageHasGraves(theBoard) ? "true" : "false", sGraveMode ? "true" : "false",
			aOver ? "true" : "false", sWinner == Winner::PLANTS ? "plantas" : sWinner == Winner::ZOMBIES ? "zombis" : "",
			sSent, sKilled, sPlantsEaten, sBrainsEaten, ClockText(aEnd), aTick - sMessageTick < sMessageTicks ? sMessage : "");

		aJson += "\"lanes\":[";
		bool aFirst = true;
		for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
		{
			if (!IsLane(theBoard, aRow))
				continue;
			aJson += std::format("{}{{\"row\":{},\"water\":{},\"brain\":{},\"grave\":{}}}", aFirst ? "" : ",", aRow,
				theBoard->mPlantRow[aRow] == PlantRowType::PLANTROW_POOL ? "true" : "false", BrainAt(theBoard, aRow) ? "true" : "false",
				GraveInLane(theBoard, aRow) ? "true" : "false");
			aFirst = false;
		}
		aJson += "],\"pages\":[";
		for (int aPage = 0; aPage < sNumPages; aPage++)
			aJson += std::format("{}\"{}\"", aPage ? "," : "", sStage->mPages[aPage].mName);
		aJson += "],\"zombies\":[";
		static constexpr const char* CHECK_NAMES[] = { "ok", "fin", "espera", "fase", "recarga", "cerebros", "carril", "tumba", "tumba" };
		for (int i = 0; i < sNumRoster; i++)
		{
			const Slot& aSlot = sRoster[i];
			SendCheck aCheck = CanSend(theBoard, i);
			int aRecharge = std::max(0, aSlot.mReadyTick - aTick);
			aJson += std::format("{}{{\"i\":{},\"page\":{},\"name\":\"{}\",\"cost\":{},\"phase\":{},\"recharge\":{:.2f},\"rechargeLeft\":{:.1f},\"state\":\"{}\"}}",
				i ? "," : "", i, aSlot.mPage, aSlot.mEntry->mName, Cost(i), aSlot.mEntry->mPhase + 1,
				aSlot.mEntry->mRecharge > 0 ? std::min(1.0f, static_cast<float>(aRecharge) / aSlot.mEntry->mRecharge) : 0.0f, aRecharge / 100.0f,
				CHECK_NAMES[static_cast<int>(aCheck)]);
		}
		aJson += "]}";
		return aJson;
	}

	void Draw(Board* theBoard, Graphics* g)
	{
		if (!IsVersus(theBoard->mApp->mGameMode) || sNumRoster == 0)
			return;

		int aTick = theBoard->mMainCounter;
		bool aOver = IsMatchOver(theBoard);

		// The selected lane, where the zombies come in (or its grave, in grave mode)
		if (!aOver && IsLane(theBoard, sLane))
		{
			int aTop = theBoard->GridToPixelY(8, sLane);
			int aHeight = sLane + 1 < MAX_GRID_SIZE_Y && IsLane(theBoard, sLane + 1) ? theBoard->GridToPixelY(8, sLane + 1) - aTop : 100;
			g->SetColor(sGraveMode ? Color(170, 90, 255, 70 + (aTick / 4) % 30) : Color(255, 40, 40, 70 + (aTick / 4) % 30));
			g->FillRect(720, aTop, 80, aHeight);
			g->SetColor(sGraveMode ? Color(190, 120, 255, 200) : Color(255, 80, 80, 200));
			g->DrawRect(720, aTop, 79, aHeight - 1);
			if (sGraveMode)
			{
				if (GridItem* aGrave = GraveInLane(theBoard, sLane))
				{
					int aX = theBoard->GridToPixelX(aGrave->mGridX, aGrave->mGridY);
					int aY = theBoard->GridToPixelY(aGrave->mGridX, aGrave->mGridY);
					g->SetColor(Color(190, 120, 255, 200));
					g->DrawRect(aX, aY, 79, aHeight - 1);
					g->DrawRect(aX + 1, aY + 1, 77, aHeight - 3);
				}
			}
		}

		// The other device: where to connect, in a strip above the bar until someone does
		if (sNet.mHasViewer && sNet.mHasViewer())
		{
			PvzpDrawString(g, "Jugador zombi conectado", 790, BAR_TOP - 5, Sexy::FONT_BRIANNETOD12, Color(120, 255, 120), DrawStringJustification::DS_ALIGN_RIGHT);
		}
		else if (sNet.mJoinText && !aOver)
		{
			std::string aJoin = sNet.mJoinText();
			if (!aJoin.empty())
			{
				g->SetColor(Color(0, 0, 0, 160));
				g->FillRect(4, BAR_TOP - 20, 592, 19);
				PvzpDrawString(g, "Jugador zombi en otro aparato: " + aJoin, 300, BAR_TOP - 6, Sexy::FONT_BRIANNETOD12, Color(200, 200, 255), DrawStringJustification::DS_ALIGN_CENTER);
			}
		}

		DrawBar(theBoard, g, aOver);

		// Bonuses and brains floating up where they happened
		for (int i = 0; i < sNumFloatTexts;)
		{
			FloatText& aText = sFloatTexts[i];
			int aAge = aTick - aText.mTick;
			if (aAge < 0 || aAge >= FLOAT_TEXT_TICKS)
			{
				std::move(sFloatTexts + i + 1, sFloatTexts + sNumFloatTexts, sFloatTexts + i);
				sNumFloatTexts--;
				continue;
			}
			Color aColor = aText.mColor;
			aColor.mAlpha = std::clamp((FLOAT_TEXT_TICKS - aAge) * 6, 0, 255);
			DrawTextShadow(g, aText.mText, aText.mX, aText.mY - aAge / 4, Sexy::FONT_HOUSEOFTERROR16, aColor, DrawStringJustification::DS_ALIGN_CENTER);
			i++;
		}

		// A message for both players, above the bar
		if (aTick - sMessageTick < sMessageTicks && !sMessage.empty() && sWinner == Winner::NONE)
		{
			int aAlpha = std::clamp((sMessageTicks - (aTick - sMessageTick)) * 4, 0, 255);
			DrawTextShadow(g, sMessage, 400, BAR_TOP - 26, Sexy::FONT_HOUSEOFTERROR16, Color(255, 220, 120, aAlpha), DrawStringJustification::DS_ALIGN_CENTER);
		}

		if (sWinner != Winner::NONE && !theBoard->mApp->GetDialog(Dialogs::DIALOG_GAME_OVER))
			DrawSummary(theBoard, g);
	}
}
