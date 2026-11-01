#ifndef __ARSCENE_H__
#define __ARSCENE_H__

#include "SexyAppFramework/misc/Rect.h"
#include <vector>

class Board;
namespace Sexy
{
	class GLInterface;
}

// The lawn as a scene of separate figures, shared by the AR page (ARView.cpp) and the diorama the game draws
// itself (ARDiorama.cpp): every object drawn alone into an atlas with GLInterface::BeginOffscreen (alpha and
// premultiplied colors), with where it stands, and the background without them. All on the GL thread.
namespace ARScene
{
	constexpr int	ATLAS_WIDTH = 1024;
	constexpr int	ATLAS_MAX_HEIGHT = 2048;
	constexpr int	BACKDROP_WIDTH = 1400;		// the whole background picture, from the house to the street
	constexpr int	BACKDROP_HEIGHT = 600;

	struct Figure
	{
		char		mKind = ' ';	// z zombie, p plant (with its pumpkin, lily pad or pot), g grave or other grid item, m mower, c sun or coin, b projectile
		int			mIndex = 0;		// with the kind, the same object from one scene to the next
		int			mRow = 0;
		float		mFeetX = 0;		// where it stands on the ground, in game coordinates
		float		mFeetY = 0;
		Sexy::Rect	mAtlas;
		int			mGameX = 0;		// the atlas rectangle's top-left on the game's screen
		int			mGameY = 0;
		int			mFacing = 1;
		int			mOrder = 0;
		int			mClickX = -1;	// where a tap on it clicks, -1 for nowhere
		int			mClickY = -1;
	};

	struct Scene
	{
		std::vector<Figure>	mFigures;
		Figure				mHud;		// flat pieces of the screen (mAtlas, mGameX, mGameY): the seed bank and menu,
		Figure				mMeter;		// the level's progress,
		Figure				mMessage;	// and a message across the lawn
		bool				mHasHud = false;
		bool				mHasMeter = false;
		bool				mHasMessage = false;
		int					mBackground = 0;
		int					mRows = 5;
		int					mLanes[6] = {};		// each row's y on the game's screen
		bool				mWater[6] = {};
		int					mUsedHeight = 0;	// atlas rows used, rounded up
		int					mSkipped = 0;		// figures left out: the atlas was full
	};

	// The lawn a diorama can show: in play, no dialog over it, one of the five outdoor backgrounds without fog yet,
	// no boss. nullptr otherwise.
	Board*	SceneBoard();

	// The background with what lies flat on it (ice, craters), BACKDROP_WIDTH x BACKDROP_HEIGHT, into theTarget
	// or read back into thePixels (RGBA, bottom-up).
	bool	PaintBackdrop(Sexy::GLInterface* theGL, Board* theBoard, std::vector<unsigned char>* thePixels, unsigned int theTarget = 0);

	// Begins the offscreen drawing (ATLAS_WIDTH x ATLAS_MAX_HEIGHT, into theTarget or the shared texture) and paints
	// every object of the lawn into it. Leaves it open: the caller reads it back or not with EndOffscreen.
	bool	PaintFigures(Sexy::GLInterface* theGL, Board* theBoard, Scene& theScene, unsigned int theTarget = 0);

	bool	DrawingFigures();	// the objects are being drawn one by one: they skip their 2D shadows
}

#endif
