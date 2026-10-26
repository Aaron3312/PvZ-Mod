#ifndef __MODVERSUS_H__
#define __MODVERSUS_H__

#include "ConstEnums.h"
#include <string>

class Board;
namespace Sexy { class Graphics; }

// "Versus" mod mode: two players. The plant player plays as usual with the mouse; the zombie player
// sends zombies from the same keyboard (W/S or the arrows pick the lane, 1-9 send a zombie) or from a
// browser on another computer (VersusNet.cpp), paying with brains that come in over time. Zombies win
// by reaching the house, plants by holding out until the clock runs out. There are no waves: every
// zombie on the lawn was sent by the zombie player.
namespace ModVersus
{
	constexpr bool IsVersus(GameMode theGameMode)
	{
		return theGameMode == GameMode::GAMEMODE_MOD_VERSUS;
	}

	constexpr int	START_BRAINS = 50;
	constexpr int	BRAIN_INCOME = 25;			// every BRAIN_INTERVAL ticks
	constexpr int	BRAIN_INTERVAL = 300;
	constexpr int	GRACE_TICKS = 1500;			// no zombies the first 15 s, so the plants can get going
	constexpr int	DURATION_TICKS = 18000;		// plants win after 3 minutes
	constexpr int	SEND_COOLDOWN = 25;			// so a held key doesn't empty the brain bank at once

	struct RosterEntry
	{
		ZombieType	mZombieType;
		int			mCost;
		const char*	mName;
	};
	constexpr int NUM_ROSTER = 9;
	const RosterEntry& GetRosterEntry(int theSlot);

	// A second computer can play the zombie side over the network (VersusNet.cpp, desktop builds only).
	// It registers these, so this always-built file doesn't depend on sockets.
	struct NetHooks
	{
		bool		(*mPopKey)(int* theKey) = nullptr;	// next key sent by the remote zombie player
		std::string	(*mJoinText)() = nullptr;			// where to connect from the other computer, "" if not listening
		bool		(*mHasViewer)() = nullptr;			// someone is watching from the other computer
	};
	void	SetNetHooks(const NetHooks& theHooks);

	void	StartLevel(Board* theBoard);				// Board::StartLevel
	void	Update(Board* theBoard);					// every playing tick, from Board::UpdateGame
	bool	KeyDown(Board* theBoard, int theKey);		// true when the key belongs to the zombie player
	void	Draw(Board* theBoard, Sexy::Graphics* g);	// Board::DrawUITop
	bool	SendZombie(Board* theBoard, int theSlot);	// into the selected lane, if there are brains for it
	int		GetBrains();
	int		GetLane();
}

#endif
