#ifndef __MODARDIORAMA_H__
#define __MODARDIORAMA_H__

#include <cstdint>

class LawnApp;

// The lawn as a 3D diorama drawn by the game itself, for AR on the phone (no browser, no Mac): the backdrop as
// the ground with the house and the sky as flats, and every plant, zombie, projectile, sun and mower as a
// standing card turned toward the camera around the vertical axis, with a soft shadow. The figures come from the
// same separate drawing ARView does (GLInterface::BeginOffscreen and friends), kept as GL textures here.
//
// Space: meters. The board is centered on the origin of its own space, the lawn on y = 0, +x along the lawn to
// the right (the street), -z toward the far edge, +z toward the player. Matrices are column-major float[16]
// (OpenGL order). All calls on the game thread with the game's GL context current.
namespace ARDiorama
{
	// Refreshes the figure atlas and the backdrop when a lawn is in play, at most every theIntervalMs.
	void	Update(LawnApp* theApp, uint32_t theNow, uint32_t theIntervalMs);

	// True while there is a lawn to draw; menus and dialogs stay 2D.
	bool	HasScene();

	// Width of the board at scale 1, in meters (the depth follows the lawn's aspect).
	float	BoardWidth();

	// How far the wooden base reaches below the lawn at scale 1, in meters: to stand it on a table at y = 0, lift
	// the board by BaseHeight() times its scale (theModel = anchor * translate(0, s * BaseHeight(), 0) * turn * scale(s)).
	float	BaseHeight();

	// Draws the diorama: theModel is board to world (placement, turn and size), theView world to eye, theProj eye
	// to clip. Depth test on; it doesn't clear and leaves the GL state as the game expects.
	void	Draw(const float theModel[16], const float theView[16], const float theProj[16]);

	// A tap along a world-space ray (theDir need not be normalized). True with the game coordinates (800x600) to
	// click when the ray hits a sun or coin card, the seed bank panel, a plant or grave card, or the lawn, in that
	// order of preference.
	bool	Pick(const float theModel[16], const float theOrigin[3], const float theDir[3], int* theGameX, int* theGameY);

	// For the desktop preview (ARDioramaPreview.cpp): a round table of theRadius meters at world y = 0, under the board.
	void	DrawTable(const float theView[16], const float theProj[16], float theRadius);

	// For tests, points of the board's own space in meters: where the game point (theGameX, theGameY) lies on the
	// lawn or on the seed bank panel, and the middle of the first sun or coin card that takes a tap.
	bool	LawnPoint(int theGameX, int theGameY, float thePoint[3]);
	bool	PanelPoint(int theGameX, int theGameY, float thePoint[3]);
	bool	CoinPoint(float thePoint[3], int* theClickX = nullptr, int* theClickY = nullptr);
}

#endif
