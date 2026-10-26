#ifndef __VERSUSNET_H__
#define __VERSUSNET_H__

// Versus over the network (desktop builds). While a versus board exists, the game serves a small web page
// on port 8090 (PVZ_VERSUS_PORT) that shows the game live and sends the zombie player's keys back, so the
// other computer only needs a browser and the address shown in the versus bar. The address carries a
// random code (PVZ_VERSUS_CODE pins it for tests); requests without it are refused.
namespace VersusNet
{
	void	OnFrame();	// on the GL thread after the game's draw (HackMenu::Render)
}

#endif
