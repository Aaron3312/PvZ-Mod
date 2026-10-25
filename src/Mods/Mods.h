#ifndef __MODS_H__
#define __MODS_H__

class LawnApp;
class Zombie;

// Cheats and mods edited from the hack menu (HackMenu.cpp) and saved in the registry.
// The game reads them through the small hooks below; everything else is applied once per tick.
struct ModSettings
{
	bool	mInfiniteSun = false;
	bool	mNoRecharge = false;
	bool	mEasyPlanting = false;			// LawnApp::mEasyPlantingCheat: free, no recharge, any tile
	bool	mInvinciblePlants = false;
	bool	mFreezeZombies = false;
	bool	mOneHitKill = false;
	bool	mInfiniteCoins = false;
	int		mCostPercent = 100;				// 0..100
	int		mDamagePercent = 100;			// 10..2000
	int		mZombieHealthPercent = 100;		// 10..1000, applied when a zombie is created
	int		mSpeed = 1;						// game updates per frame, 1..10
};

namespace Mods
{
	ModSettings&	Settings();
	void			Load(LawnApp* theApp);
	void			Save(LawnApp* theApp);

	void			OnUpdate(LawnApp* theApp);	// once per game tick
	int				UpdatesPerFrame();

	int				ScaleCost(int theCost);
	int				ScaleDamage(int theDamage);
	void			ScaleZombieHealth(Zombie* theZombie);
}

#endif
