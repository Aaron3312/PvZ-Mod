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

	// Draws the diorama: theModel is board to world (placement, turn and size), theView world to eye, theProj eye
	// to clip. Depth test on; it doesn't clear and leaves the GL state as the game expects.
	void	Draw(const float theModel[16], const float theView[16], const float theProj[16]);

	// A tap along a world-space ray. True with the game coordinates (800x600) to click when the ray hits a sun or
	// coin card, the seed bank panel, a plant or grave card, or the lawn, in that order of preference.
	bool	Pick(const float theModel[16], const float theOrigin[3], const float theDir[3], int* theGameX, int* theGameY);
}

#endif
