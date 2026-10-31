#ifndef __MODVERSUS_H__
#define __MODVERSUS_H__

#include "ConstEnums.h"
#include <string>

class Board;
class Plant;
class Zombie;
namespace Sexy { class Graphics; }

// "Versus" mod modes, one per stage (day, night, pool, fog, roof): two players. The plant player plays as
// usual with the mouse; the zombie player sends zombies from the same keyboard (W/S or the arrows pick the
// lane, Q/E the page of the roster, 1-9 send a zombie) or from a browser on another device (VersusNet.cpp),
// paying with brains that come in over time. There are no waves: every zombie on the lawn was sent by the
// zombie player. The match runs in three phases; each one pays more brains and unlocks heavier zombies.
// Each lane has a brain by the house: once its mower is gone, a zombie that gets to the house eats it.
// Zombies win by eating BRAINS_TO_WIN brains, plants by holding out until the clock runs out.
namespace ModVersus
{
	constexpr bool IsVersus(GameMode theGameMode)
	{
		return theGameMode >= GameMode::GAMEMODE_MOD_VERSUS && theGameMode <= GameMode::GAMEMODE_MOD_VERSUS_ROOF;
	}

	constexpr int	NUM_PHASES = 3;
	constexpr int	DURATION_TICKS = 48000;				// 8 minutes
	constexpr int	PHASE_START_PERCENT[NUM_PHASES] = { 0, 30, 70 };	// of the match: 0:00, 2:24 and 5:36
	constexpr int	GRACE_TICKS = 2000;					// no zombies the first 20 s, so the plants can get going
	constexpr int	START_BRAINS = 75;
	constexpr int	BRAIN_INTERVAL = 300;				// the zombie player gets brains every 3 s...
	constexpr int	BRAIN_INCOME[NUM_PHASES] = { 25, 35, 50 };	// ...this many in each phase
	constexpr int	BRAINS_TO_WIN = 3;					// house brains the zombies must eat
	constexpr int	PLANT_EATEN_BONUS_PERCENT = 25;		// brains when a zombie eats a plant, of the plant's cost...
	constexpr int	PLANT_EATEN_BONUS_MIN = 10;
	constexpr int	PLANT_EATEN_BONUS_MAX = 50;
	constexpr int	KILL_REWARD_MIN_COST = 150;			// sun for the plants when they kill a zombie this expensive...
	constexpr int	KILL_REWARD_PERCENT = 25;			// ...this share of its cost, in 25-sun drops
	constexpr int	GRAVE_COST_PERCENT = 150;			// night: rising from a grave costs this much of the price
	constexpr int	GRAVE_RECHARGE = 500;				// between two rises
	constexpr int	GRAVE_BUSTED_SUN = 75;				// night: sun for the plants for each grave they clear
	constexpr int	WIN_PAUSE_TICKS = 350;				// the zombies' win shows on the lawn before the game-over dialog
	constexpr int	PAGE_SLOTS = 9;
	constexpr int	MAX_PAGES = 4;
	constexpr int	MAX_ROSTER = PAGE_SLOTS * MAX_PAGES;

	// Commands from the other device that aren't keys (VersusNet's /cmd?z= and ?l=)
	constexpr int	NET_SEND = 0x10000;					// + roster index
	constexpr int	NET_LANE = 0x20000;					// + row

	struct RosterEntry
	{
		ZombieType	mZombieType;
		int			mCost;
		int			mRecharge;		// ticks before the same zombie can be sent again
		int			mPhase;			// first phase (0-2) it can be sent in
		const char*	mName;
	};

	// Why a zombie can't be sent right now (the bar and the web page grey it out)
	enum class SendCheck
	{
		OK,
		OVER,
		GRACE,
		LOCKED,				// a later phase
		RECHARGING,
		BRAINS,
		LANE,				// can't go in the selected lane (water, no ice, ...)
		NO_GRAVE,			// grave mode: no grave in the lane, or this zombie can't rise from one
		GRAVE_RECHARGING
	};

	// A second device can play the zombie side over the network (VersusNet.cpp, desktop and Android builds).
	// It registers these, so this always-built file doesn't depend on sockets.
	struct NetHooks
	{
		bool		(*mPopKey)(int* theKey) = nullptr;	// next key or NET_ command sent by the remote zombie player
		std::string	(*mJoinText)() = nullptr;			// where to connect from the other device, "" if not listening
		bool		(*mHasViewer)() = nullptr;			// someone is watching from the other device
	};
	void	SetNetHooks(const NetHooks& theHooks);

	void	AllowZombies(GameMode theGameMode, bool* theAllowed);	// Challenge::InitZombieWaves: the stage's roster
	void	StartLevel(Board* theBoard);				// Board::StartLevel
	void	Update(Board* theBoard);					// every playing tick, from Board::UpdateGame
	bool	KeyDown(Board* theBoard, int theKey);		// true when the key belongs to the zombie player
	void	Draw(Board* theBoard, Sexy::Graphics* g);	// Board::DrawUITop
	bool	ZombieReachedHouse(Zombie* theZombie);		// Zombie::CheckForBoardEdge: true when the versus took over
	void	OnZombieKilled(Zombie* theZombie);			// ModContent::OnZombieKilled
	void	OnPlantEaten(Plant* thePlant);				// Zombie::EatPlant, when the plant dies
	std::string ZombiesWonMessage();					// Board::ZombiesWon: the match summary for the game-over dialog
	std::string StateJson(Board* theBoard);				// VersusNet's /state, built on the game thread

	int		GetRosterSize();
	const RosterEntry& GetRosterEntry(int theIndex);
	bool	SendZombie(Board* theBoard, int theIndex);	// into the selected lane, if it can go now
	SendCheck CanSend(Board* theBoard, int theIndex);
	int		GetBrains();
	int		GetLane();
}

#endif
