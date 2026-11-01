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
	bool	mARDiorama = false;				// the lawn as a 3D diorama in the game's window (ARDioramaPreview.cpp)
	bool	mARView = false;				// serve the game to a phone in AR (ARView.cpp)
	bool	mARNative = false;				// AR on this phone with ARCore (ARNative.cpp, Android)
};

namespace Mods
{
	ModSettings&	Settings();
	void			Load(LawnApp* theApp);
	void			Save(LawnApp* theApp);

	void			OnUpdate(LawnApp* theApp);	// once per game tick
	int				UpdatesPerFrame();

	// A versus match ignores the cheats that tilt it (all but the speed) without changing the saved settings.
	bool			CheatsSuspended();
	bool			MatchCheatsOn();			// one of those is set

	int				ScaleCost(int theCost);
	int				ScaleDamage(int theDamage);
	void			ScaleZombieHealth(Zombie* theZombie);
}

#endif
