#include "ARScene.h"
#include "LawnApp.h"
#include "Resources.h"
#include "Lawn/Board.h"
#include "Lawn/Coin.h"
#include "Lawn/GridItem.h"
#include "Lawn/LawnMower.h"
#include "Lawn/MessageWidget.h"
#include "Lawn/Plant.h"
#include "Lawn/Projectile.h"
#include "Lawn/Zombie.h"
#include "SexyAppFramework/graphics/GLImage.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/Graphics.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

using namespace ARScene;

namespace
{
	constexpr int	ATLAS_PAD = 3;
	constexpr int	ATLAS_HEIGHT_STEP = 64;		// fewer texture sizes on the phone
	constexpr int	FIGURE_MAX_WIDTH = 480;
	constexpr int	FIGURE_MAX_HEIGHT = 560;
	constexpr int	FIGURE_PROBE = 4096;		// drawn around here first, far from any clipping, then moved into the atlas

	bool						gDrawingFigures = false;
	std::unique_ptr<GLImage>	gCanvas;		// a big destination, so figures drawn at the probe aren't clipped

	// At exit the GL interface is gone before the statics are destroyed, and the canvas would reach it from its
	// destructor (RemoveGLImage on a dead mutex aborted the game on quit). It is left alone then.
	void ReleaseCanvasAtExit()
	{
		gCanvas.release();
	}

	GLImage* Canvas(GLInterface* theGL)
	{
		if (!gCanvas)
		{
			gCanvas = std::make_unique<GLImage>(theGL);
			gCanvas->mWidth = FIGURE_PROBE * 2;
			gCanvas->mHeight = FIGURE_PROBE * 2;
			gCanvas->SetImageMode(false, false);
			std::atexit(ReleaseCanvasAtExit);
		}
		return gCanvas.get();
	}

	// Draws objects alone: first around the probe point, then measures what was drawn, moves it into the next free
	// place of the atlas and sends it clipped there.
	class Painter
	{
	public:
		GLInterface*	mGL;
		Graphics		mBase;
		int				mX = ATLAS_PAD;
		int				mY = ATLAS_PAD;
		int				mShelf = 0;
		int				mSkipped = 0;

		Painter(GLInterface* theGL) : mGL(theGL), mBase(Canvas(theGL))
		{
			mBase.SetLinearBlend(true);
		}

		int UsedHeight() const
		{
			int aHeight = mY + mShelf + ATLAS_PAD;
			return std::min(ATLAS_MAX_HEIGHT, (aHeight + ATLAS_HEIGHT_STEP - 1) / ATLAS_HEIGHT_STEP * ATLAS_HEIGHT_STEP);
		}

		template <class T> bool Measure(T theDraw, float& theLeft, float& theTop, float& theRight, float& theBottom)
		{
			mGL->DropPending();
			Graphics g(mBase);
			g.mTransX = FIGURE_PROBE;
			g.mTransY = FIGURE_PROBE;
			theDraw(&g);
			bool aDrawn = mGL->PendingBounds(theLeft, theTop, theRight, theBottom);
			mGL->DropPending();
			theLeft -= FIGURE_PROBE;
			theRight -= FIGURE_PROBE;
			theTop -= FIGURE_PROBE;
			theBottom -= FIGURE_PROBE;
			return aDrawn;
		}

		// A panel (thePanel) keeps only what is on the game's screen, as wide as the atlas allows; a figure keeps the
		// part around its middle and bottom when it is too big.
		template <class T> bool Paint(T theDraw, Figure& theFigure, bool thePanel = false)
		{
			mGL->DropPending();
			Graphics g(mBase);
			g.mTransX = FIGURE_PROBE;
			g.mTransY = FIGURE_PROBE;
			theDraw(&g);
			float l, t, r, b;
			if (!mGL->PendingBounds(l, t, r, b))
				return false;
			int x0 = static_cast<int>(std::floor(l)), y0 = static_cast<int>(std::floor(t));
			int x1 = static_cast<int>(std::ceil(r)), y1 = static_cast<int>(std::ceil(b));
			if (thePanel)
			{
				x0 = std::max(x0, FIGURE_PROBE);
				y0 = std::max(y0, FIGURE_PROBE);
				x1 = std::min({ x1, FIGURE_PROBE + BOARD_WIDTH, x0 + ATLAS_WIDTH - 2 * ATLAS_PAD });
				y1 = std::min(y1, FIGURE_PROBE + BOARD_HEIGHT);
			}
			if (x1 - x0 > FIGURE_MAX_WIDTH && !thePanel)
			{
				x0 = (x0 + x1 - FIGURE_MAX_WIDTH) / 2;
				x1 = x0 + FIGURE_MAX_WIDTH;
			}
			if (y1 - y0 > FIGURE_MAX_HEIGHT)
				y0 = y1 - FIGURE_MAX_HEIGHT;	// the bottom is what stands on the lawn
			int aWidth = x1 - x0, aHeight = y1 - y0;
			if (aWidth <= 0 || aHeight <= 0)
			{
				mGL->DropPending();
				return false;
			}
			if (mX + aWidth + ATLAS_PAD > ATLAS_WIDTH)
			{
				mX = ATLAS_PAD;
				mY += mShelf + ATLAS_PAD;
				mShelf = 0;
			}
			if (mY + aHeight + ATLAS_PAD > ATLAS_MAX_HEIGHT)
			{
				mGL->DropPending();
				mSkipped++;
				return false;
			}
			mGL->MovePending(static_cast<float>(mX - x0), static_cast<float>(mY - y0));
			theFigure.mAtlas = Rect(mX, mY, aWidth, aHeight);
			mGL->FlushOffscreen(theFigure.mAtlas);
			theFigure.mGameX = x0 - FIGURE_PROBE;
			theFigure.mGameY = y0 - FIGURE_PROBE;
			mX += aWidth + ATLAS_PAD;
			mShelf = std::max(mShelf, aHeight);
			return true;
		}
	};

	void PaintObjects(GLInterface* theGL, Board* theBoard, Scene& theScene)
	{
		Painter aPainter(theGL);
		std::vector<Figure>& aFigures = theScene.mFigures;
		theScene.mHasHud = aPainter.Paint([&](Graphics* g) {
			theBoard->DrawUIBottom(g);
			if (theBoard->StageHasFog())
				theBoard->DrawTopRightUI(g);
		}, theScene.mHud, true);
		theScene.mHasMeter = aPainter.Paint([&](Graphics* g) {
			theBoard->DrawProgressMeter(g);
			theBoard->DrawLevel(g);
		}, theScene.mMeter, true);
		theScene.mHasMessage = aPainter.Paint([&](Graphics* g) { theBoard->mAdvice->Draw(g); }, theScene.mMessage, true);
		bool aWhack = gLawnApp->IsWhackAZombieLevel();

		for (Zombie* aZombie : theBoard->mZombies)
		{
			if (aZombie->mDead || aZombie->mZombieType == ZombieType::ZOMBIE_BOSS)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) {
					if (aZombie->BeginDraw(g))
					{
						aZombie->Draw(g);
						aZombie->EndDraw(g);
					}
				}, aFigure))
				continue;
			// It stands where its shadow is, which stays on the ground while it jumps, flies or climbs
			float l, t, r, b;
			if (aZombie->HasShadow() && aPainter.Measure([&](Graphics* g) {
					if (aZombie->BeginDraw(g))
					{
						aZombie->DrawShadow(g);
						aZombie->EndDraw(g);
					}
				}, l, t, r, b))
			{
				aFigure.mFeetX = (l + r) / 2;
				aFigure.mFeetY = (t + b) / 2;
			}
			else
			{
				aFigure.mFeetX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2.0f;
				aFigure.mFeetY = static_cast<float>(aFigure.mGameY + aFigure.mAtlas.mHeight);
			}
			aFigure.mKind = 'z';
			aFigure.mIndex = static_cast<int>(theBoard->mZombies.DataArrayGetID(aZombie));
			aFigure.mRow = aZombie->mRow;
			aFigure.mFacing = aZombie->IsWalkingBackwards() ? 1 : -1;
			aFigure.mOrder = aZombie->mRenderOrder;
			if (aWhack)
			{
				aFigure.mClickX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2;
				aFigure.mClickY = aFigure.mGameY + aFigure.mAtlas.mHeight / 2;
			}
			aFigures.push_back(aFigure);
		}

		// Plants by cell, so a pumpkin, a lily pad or a pot goes with what stands in it
		std::vector<Plant*> aPlants;
		for (Plant* aPlant : theBoard->mPlants)
		{
			if (!aPlant->mDead && aPlant->mOnBungeeState == PlantOnBungeeState::NOT_ON_BUNGEE)
				aPlants.push_back(aPlant);
		}
		std::sort(aPlants.begin(), aPlants.end(), [](Plant* a, Plant* b) {
			if (a->mPlantCol != b->mPlantCol || a->mRow != b->mRow)
				return a->mPlantCol * MAX_GRID_SIZE_Y + a->mRow < b->mPlantCol * MAX_GRID_SIZE_Y + b->mRow;
			return a->mRenderOrder < b->mRenderOrder;
		});
		for (size_t i = 0; i < aPlants.size();)
		{
			size_t aEnd = i + 1;
			while (aEnd < aPlants.size() && aPlants[aEnd]->mPlantCol == aPlants[i]->mPlantCol && aPlants[aEnd]->mRow == aPlants[i]->mRow)
				aEnd++;
			Figure aFigure;
			bool aDrawn = aPainter.Paint([&](Graphics* g) {
				for (size_t j = i; j < aEnd; j++)
				{
					Plant* aPlant = aPlants[j];
					if (aPlant->BeginDraw(g))
					{
						aPlant->Draw(g);
						if ((aPlant->mSeedType == SeedType::SEED_MAGNETSHROOM || aPlant->mSeedType == SeedType::SEED_GOLD_MAGNET) && aPlant->DrawMagnetItemsOnTop())
							aPlant->DrawMagnetItems(g);
						aPlant->EndDraw(g);
					}
				}
			}, aFigure);
			if (aDrawn)
			{
				Plant* aMain = aPlants[aEnd - 1];
				int aCol = aMain->mPlantCol, aRow = aMain->mRow;
				bool aOnGrid = aCol >= 0 && aCol < MAX_GRID_SIZE_X && aRow >= 0 && aRow < MAX_GRID_SIZE_Y;
				int aCellY = aOnGrid ? theBoard->GridToPixelY(aCol, aRow) : aMain->mY;
				aFigure.mFeetX = aMain->mX + (aMain->mSeedType == SeedType::SEED_COBCANNON ? 80.0f : 40.0f);
				aFigure.mFeetY = aCellY + 69.0f;
				aFigure.mKind = 'p';
				aFigure.mIndex = aCol * MAX_GRID_SIZE_Y + aRow;
				aFigure.mRow = aRow;
				aFigure.mOrder = aMain->mRenderOrder;
				aFigure.mClickX = static_cast<int>(aFigure.mFeetX);
				aFigure.mClickY = aCellY + 40;
				aFigures.push_back(aFigure);
			}
			i = aEnd;
		}

		for (GridItem* aGridItem : theBoard->mGridItems)
		{
			if (aGridItem->mDead || aGridItem->mGridItemType == GridItemType::GRIDITEM_CRATER)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) { aGridItem->DrawGridItem(g); }, aFigure))
				continue;
			aFigure.mFeetX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2.0f;
			aFigure.mFeetY = static_cast<float>(aFigure.mGameY + aFigure.mAtlas.mHeight);
			aFigure.mKind = 'g';
			aFigure.mIndex = static_cast<int>(theBoard->mGridItems.DataArrayGetID(aGridItem));
			aFigure.mRow = aGridItem->mGridY;
			aFigure.mOrder = aGridItem->mRenderOrder;
			if (aGridItem->mGridX >= 0 && aGridItem->mGridX < MAX_GRID_SIZE_X && aGridItem->mGridY >= 0 && aGridItem->mGridY < MAX_GRID_SIZE_Y)
			{
				aFigure.mClickX = theBoard->GridToPixelX(aGridItem->mGridX, aGridItem->mGridY) + 40;
				aFigure.mClickY = theBoard->GridToPixelY(aGridItem->mGridX, aGridItem->mGridY) + 40;
			}
			aFigures.push_back(aFigure);
		}

		for (LawnMower* aLawnMower : theBoard->mLawnMowers)
		{
			if (aLawnMower->mDead)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) { aLawnMower->Draw(g); }, aFigure))
				continue;
			// It stands where LawnMower::Draw puts its shadow: the picture reaches well below the wheels
			float aShadowX = aLawnMower->mPosX - 7.0f, aShadowY = aLawnMower->mPosY - aLawnMower->mAltitude + 47.0f;
			if (aLawnMower->mMowerType == LawnMowerType::LAWNMOWER_POOL)
			{
				aShadowX -= 17.0f;
				aShadowY -= 8.0f;
			}
			else if (aLawnMower->mMowerType == LawnMowerType::LAWNMOWER_ROOF)
			{
				aShadowX -= 9.0f;
				aShadowY -= aLawnMower->mMowerState == LawnMowerState::MOWER_TRIGGERED ? 0.0f : 36.0f;
			}
			aFigure.mFeetX = aShadowX + IMAGE_PLANTSHADOW->GetCelWidth() / 2.0f;
			aFigure.mFeetY = aShadowY + IMAGE_PLANTSHADOW->GetCelHeight() / 2.0f;
			aFigure.mKind = 'm';
			aFigure.mIndex = static_cast<int>(theBoard->mLawnMowers.DataArrayGetID(aLawnMower));
			aFigure.mRow = aLawnMower->mRow;
			aFigure.mOrder = aLawnMower->mRenderOrder;
			aFigures.push_back(aFigure);
		}

		for (Coin* aCoin : theBoard->mCoins)
		{
			if (aCoin->mDead)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) {
					if (aCoin->BeginDraw(g))
					{
						aCoin->Draw(g);
						aCoin->EndDraw(g);
					}
				}, aFigure))
				continue;
			// Up in the air by as much as it still has to fall (or has risen, on its way to the counter)
			aFigure.mFeetX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2.0f;
			aFigure.mFeetY = aFigure.mGameY + aFigure.mAtlas.mHeight + std::max(0.0f, aCoin->mGroundY - aCoin->mPosY);
			aFigure.mKind = 'c';
			aFigure.mIndex = static_cast<int>(theBoard->mCoins.DataArrayGetID(aCoin));
			aFigure.mRow = aCoin->mRow;
			aFigure.mOrder = aCoin->mRenderOrder;
			if (!aCoin->mIsBeingCollected)
			{
				aFigure.mClickX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2;
				aFigure.mClickY = aFigure.mGameY + aFigure.mAtlas.mHeight / 2;
			}
			aFigures.push_back(aFigure);
		}

		for (Projectile* aProjectile : theBoard->mProjectiles)
		{
			if (aProjectile->mDead)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) {
					if (aProjectile->BeginDraw(g))
					{
						aProjectile->Draw(g);
						aProjectile->EndDraw(g);
					}
				}, aFigure))
				continue;
			float l, t, r, b;
			if (aPainter.Measure([&](Graphics* g) {
					if (aProjectile->BeginDraw(g))
					{
						aProjectile->DrawShadow(g);
						aProjectile->EndDraw(g);
					}
				}, l, t, r, b))
			{
				aFigure.mFeetX = (l + r) / 2;
				aFigure.mFeetY = (t + b) / 2;
			}
			else
			{
				aFigure.mFeetX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2.0f;
				aFigure.mFeetY = aProjectile->mShadowY + 5.0f;
			}
			aFigure.mKind = 'b';
			aFigure.mIndex = static_cast<int>(theBoard->mProjectiles.DataArrayGetID(aProjectile));
			aFigure.mRow = aProjectile->mRow;
			aFigure.mFacing = aProjectile->mVelX < 0 ? -1 : 1;
			aFigure.mOrder = aProjectile->mRenderOrder;
			aFigures.push_back(aFigure);
		}

		theScene.mRows = theBoard->StageHasPool() ? 6 : 5;
		for (int aRow = 0; aRow < theScene.mRows; aRow++)
		{
			theScene.mLanes[aRow] = theBoard->GridToPixelY(8, aRow);
			theScene.mWater[aRow] = theBoard->mPlantRow[aRow] == PlantRowType::PLANTROW_POOL;
		}
		theScene.mBackground = static_cast<int>(theBoard->mBackground);
		theScene.mUsedHeight = aPainter.UsedHeight();
		theScene.mSkipped = aPainter.mSkipped;
	}
}

Board* ARScene::SceneBoard()
{
	Board* aBoard = gLawnApp ? gLawnApp->mBoard : nullptr;
	if (!aBoard || gLawnApp->mGameScene != GameScenes::SCENE_PLAYING || gLawnApp->GetDialogCount() > 0 || aBoard->StageHasFog())
		return nullptr;
	if (aBoard->mBackground < BackgroundType::BACKGROUND_1_DAY || aBoard->mBackground > BackgroundType::BACKGROUND_5_ROOF)
		return nullptr;
	if (gLawnApp->IsFinalBossLevel() || aBoard->mX != 0 || aBoard->mY != 0)
		return nullptr;
	return aBoard;
}

bool ARScene::PaintBackdrop(GLInterface* theGL, Board* theBoard, std::vector<unsigned char>* thePixels, unsigned int theTarget)
{
	GLImage* aCanvas = Canvas(theGL);
	if (!theGL->BeginOffscreen(BACKDROP_WIDTH, BACKDROP_HEIGHT, theTarget))
		return false;
	Graphics g(aCanvas);
	g.SetLinearBlend(true);
	g.mTransX = BOARD_OFFSET;
	theBoard->DrawBackdrop(&g);
	for (GridItem* aGridItem : theBoard->mGridItems)
	{
		if (!aGridItem->mDead && aGridItem->mGridItemType == GridItemType::GRIDITEM_CRATER)
			aGridItem->DrawGridItem(&g);
	}
	for (int aRow = 0; aRow < MAX_GRID_SIZE_Y; aRow++)
	{
		if (theBoard->mIceTimer[aRow])
			theBoard->DrawIce(&g, aRow);
	}
	theGL->FlushOffscreen(Rect(0, 0, BACKDROP_WIDTH, BACKDROP_HEIGHT));
	theGL->EndOffscreen(thePixels, BACKDROP_WIDTH, BACKDROP_HEIGHT);
	return true;
}

bool ARScene::PaintFigures(GLInterface* theGL, Board* theBoard, Scene& theScene, unsigned int theTarget)
{
	Canvas(theGL);
	if (!theGL->BeginOffscreen(ATLAS_WIDTH, ATLAS_MAX_HEIGHT, theTarget))
		return false;
	theScene.mFigures.clear();
	gDrawingFigures = true;
	PaintObjects(theGL, theBoard, theScene);
	gDrawingFigures = false;
	return true;
}

bool ARScene::DrawingFigures()
{
	return gDrawingFigures;
}
