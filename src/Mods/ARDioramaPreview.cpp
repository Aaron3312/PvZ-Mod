#include "ARDioramaPreview.h"
#include "ARDiorama.h"
#include "HackMenu.h"
#include "Mods.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Coin.h"
#include "Lawn/CursorObject.h"
#include "Lawn/Plant.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Zombie.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include "SexyAppFramework/widget/WidgetManager.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace
{
	constexpr uint32_t	UPDATE_INTERVAL_MS = 85;	// about 11 scenes a second, like the page's
	constexpr float		FOV_Y = 45.0f;
	constexpr float		TABLE_RADIUS = 0.5f;
	constexpr float		DEG = 3.14159265f / 180;

	struct Orbit
	{
		float	mYaw = 0;			// radians, 0 is in front of the board (+z)
		float	mPitch = 38 * DEG;
		float	mDist = 0.62f;		// meters to the target
	};

	bool		gShowing = false;
	Orbit		gOrbit;
	float		gSpin = 0;			// PVZ_AR_DIORAMA_ORBIT: degrees a second, for shots from all around
	uint32_t	gLastFrame = 0;
	bool		gPressed = false;
	bool		gMoved = false;
	int			gDownX = 0;
	int			gDownY = 0;
	int			gLastX = 0;
	int			gLastY = 0;
	float		gModel[16];
	float		gView[16];
	float		gProj[16];

	void Identity(float m[16])
	{
		for (int i = 0; i < 16; i++)
			m[i] = i % 5 == 0 ? 1.0f : 0.0f;
	}

	void MulMat(const float a[16], const float b[16], float theOut[16])
	{
		float r[16];
		for (int c = 0; c < 4; c++)
			for (int i = 0; i < 4; i++)
				r[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
		std::copy(r, r + 16, theOut);
	}

	void Transform(const float m[16], const float p[4], float theOut[4])
	{
		for (int i = 0; i < 4; i++)
			theOut[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i] * p[3];
	}

	bool InvertMat(const float m[16], float theOut[16])
	{
		// Gauss-Jordan on a copy, column-major in and out
		float a[4][8];
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
			{
				a[r][c] = m[c * 4 + r];
				a[r][c + 4] = r == c ? 1.0f : 0.0f;
			}
		for (int c = 0; c < 4; c++)
		{
			int aPivot = c;
			for (int r = c + 1; r < 4; r++)
				if (std::fabs(a[r][c]) > std::fabs(a[aPivot][c]))
					aPivot = r;
			if (std::fabs(a[aPivot][c]) < 1e-12f)
				return false;
			std::swap(a[c], a[aPivot]);
			float aScale = 1 / a[c][c];
			for (int k = 0; k < 8; k++)
				a[c][k] *= aScale;
			for (int r = 0; r < 4; r++)
			{
				if (r == c)
					continue;
				float f = a[r][c];
				for (int k = 0; k < 8; k++)
					a[r][k] -= f * a[c][k];
			}
		}
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				theOut[c * 4 + r] = a[r][c + 4];
		return true;
	}

	// The camera orbiting the board's middle; the board stands on a table at y = 0, lifted by its base
	void PlaceCamera(int theWidth, int theHeight)
	{
		gOrbit.mPitch = std::clamp(gOrbit.mPitch, 0.05f, 1.5f);
		gOrbit.mDist = std::clamp(gOrbit.mDist, 0.15f, 2.5f);
		float aTarget[3] = { 0, 0.02f, 0.01f };
		float c = std::cos(gOrbit.mPitch);
		float aEye[3] = { aTarget[0] + gOrbit.mDist * c * std::sin(gOrbit.mYaw), aTarget[1] + gOrbit.mDist * std::sin(gOrbit.mPitch),
			aTarget[2] + gOrbit.mDist * c * std::cos(gOrbit.mYaw) };
		float f[3] = { aTarget[0] - aEye[0], aTarget[1] - aEye[1], aTarget[2] - aEye[2] };
		float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
		for (float& v : f)
			v /= fl;
		float s[3] = { -f[2], 0, f[0] };	// f x up
		float sl = std::sqrt(s[0] * s[0] + s[2] * s[2]);
		s[0] /= sl;
		s[2] /= sl;
		float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
		Identity(gView);
		gView[0] = s[0], gView[4] = s[1], gView[8] = s[2];
		gView[1] = u[0], gView[5] = u[1], gView[9] = u[2];
		gView[2] = -f[0], gView[6] = -f[1], gView[10] = -f[2];
		gView[12] = -(s[0] * aEye[0] + s[1] * aEye[1] + s[2] * aEye[2]);
		gView[13] = -(u[0] * aEye[0] + u[1] * aEye[1] + u[2] * aEye[2]);
		gView[14] = f[0] * aEye[0] + f[1] * aEye[1] + f[2] * aEye[2];

		float aNear = 0.02f, aFar = 10.0f, aFocal = 1 / std::tan(FOV_Y * DEG / 2);
		float aAspect = theHeight > 0 ? static_cast<float>(theWidth) / theHeight : 4.0f / 3;
		std::fill(gProj, gProj + 16, 0.0f);
		gProj[0] = aFocal / aAspect;
		gProj[5] = aFocal;
		gProj[10] = (aFar + aNear) / (aNear - aFar);
		gProj[11] = -1;
		gProj[14] = 2 * aFar * aNear / (aNear - aFar);

		Identity(gModel);
		gModel[13] = ARDiorama::BaseHeight();
	}

	// What SexyAppBase does with a click, minus RemapMouse: these are game coordinates already. A click counts as
	// focused, as ARView's taps do (buttons ignore a release without focus, and a test window never has it).
	void SendClick(int theX, int theY)
	{
		if (!gLawnApp || !gLawnApp->mWidgetManager)
			return;
		WidgetManager* aManager = gLawnApp->mWidgetManager.get();
		int aX = std::clamp(theX, 0, gLawnApp->mWidth - 1);
		int aY = std::clamp(theY, 0, gLawnApp->mHeight - 1);
		gLawnApp->mMouseIn = true;
		gLawnApp->mLastUserInputTick = gLawnApp->mLastTimerTime;
		bool aHadFocus = aManager->mHasFocus;
		aManager->mHasFocus = true;
		aManager->MouseMove(aX, aY);
		aManager->MouseDown(aX, aY, 1);
		aManager->MouseUp(aX, aY, 1);
		aManager->mHasFocus = aHadFocus;
	}

	// A click on the window (its own coordinates) as a ray through the diorama; the game point it clicked, if any
	bool Click(int theWindowX, int theWindowY, int* theGameX = nullptr, int* theGameY = nullptr)
	{
		int aWidth = 0, aHeight = 0;
		SDL_GetWindowSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
		if (aWidth <= 0 || aHeight <= 0)
			return false;
		float aViewProj[16], aInverse[16];
		MulMat(gProj, gView, aViewProj);
		if (!InvertMat(aViewProj, aInverse))
			return false;
		float x = 2.0f * theWindowX / aWidth - 1, y = 1 - 2.0f * theWindowY / aHeight;
		float aNear[4] = { x, y, -1, 1 }, aFar[4] = { x, y, 1, 1 }, n[4], f[4];
		Transform(aInverse, aNear, n);
		Transform(aInverse, aFar, f);
		float aOrigin[3] = { n[0] / n[3], n[1] / n[3], n[2] / n[3] };
		float aDir[3] = { f[0] / f[3] - aOrigin[0], f[1] / f[3] - aOrigin[1], f[2] / f[3] - aOrigin[2] };
		int aGameX = 0, aGameY = 0;
		bool aHit = ARDiorama::Pick(gModel, aOrigin, aDir, &aGameX, &aGameY);
		static const bool sLog = SDL_getenv("PVZ_AR_DIORAMA_TEST") != nullptr;
		if (sLog)
			PvzpLogLn("ARDiorama test: click at window {},{} -> {}", theWindowX, theWindowY, aHit ? std::format("game {},{}", aGameX, aGameY) : "nothing");
		if (!aHit)
			return false;
		SendClick(aGameX, aGameY);
		if (theGameX)
			*theGameX = aGameX;
		if (theGameY)
			*theGameY = aGameY;
		return true;
	}

	// ---- PVZ_AR_DIORAMA_TEST: shots from around the board, then a sun, a seed and a plant clicked through Pick ----

	struct TestView
	{
		const char*	mName;
		float		mYaw;
		float		mPitch;
		float		mDist;
	};

	const TestView TEST_VIEWS[] = {
		{ "frente", 0, 38, 0.62f },
		{ "izquierda", -50, 30, 0.6f },
		{ "derecha", 55, 26, 0.58f },
		{ "arriba", 0, 80, 0.72f },
		{ "bajo", 18, 9, 0.5f },
		{ "atras", 160, 32, 0.62f },
		{ "cerca", -25, 22, 0.32f },
	};

	const char* BackgroundName()
	{
		static const char* sNames[] = { "dia", "noche", "piscina", "niebla", "tejado" };
		int aType = gLawnApp->mBoard ? static_cast<int>(gLawnApp->mBoard->mBackground) : -1;
		return aType >= 0 && aType < 5 ? sNames[aType] : "otro";
	}

	void Shot(const std::string& theName)
	{
		if (const char* aFolder = SDL_getenv("PVZ_MOD_TEST_SHOTS"))
			HackMenu::RequestScreenshot(std::format("{}/dio-{}-{}.png", aFolder, BackgroundName(), theName).c_str());
	}

	// A point of the board's space on the window
	bool Project(const float thePoint[3], int& theX, int& theY)
	{
		int aWidth = 0, aHeight = 0;
		SDL_GetWindowSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
		float aMv[16], aMvp[16], p[4] = { thePoint[0], thePoint[1], thePoint[2], 1 }, c[4];
		MulMat(gView, gModel, aMv);
		MulMat(gProj, aMv, aMvp);
		Transform(aMvp, p, c);
		if (c[3] <= 0)
			return false;
		theX = static_cast<int>(std::lround((c[0] / c[3] + 1) / 2 * aWidth));
		theY = static_cast<int>(std::lround((1 - c[1] / c[3]) / 2 * aHeight));
		return theX >= 0 && theX < aWidth && theY >= 0 && theY < aHeight;
	}

	// A mouse button through SDL's queue, so it takes the same way as the real mouse (ProcessEvent)
	void PushMouse(Uint32 theType, int theX, int theY, bool theHeld)
	{
		SDL_Event aEvent = {};
		aEvent.type = theType;
		if (theType == SDL_MOUSEMOTION)
		{
			aEvent.motion.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(gLawnApp->mWindow));
			aEvent.motion.x = theX;
			aEvent.motion.y = theY;
			aEvent.motion.state = theHeld ? SDL_BUTTON_LMASK : 0;
		}
		else
		{
			aEvent.button.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(gLawnApp->mWindow));
			aEvent.button.button = SDL_BUTTON_LEFT;
			aEvent.button.state = theType == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
			aEvent.button.clicks = 1;
			aEvent.button.x = theX;
			aEvent.button.y = theY;
		}
		SDL_PushEvent(&aEvent);
	}

	bool ClickPoint(const char* theWhat, const float thePoint[3], int theExpectX, int theExpectY)
	{
		int x = 0, y = 0;
		if (!Project(thePoint, x, y))
		{
			PvzpLogLn("ARDiorama test: {} is off the window", theWhat);
			return false;
		}
		PvzpLogLn("ARDiorama test: clicking {} (game {},{} expected)", theWhat, theExpectX, theExpectY);
		PushMouse(SDL_MOUSEBUTTONDOWN, x, y, true);
		PushMouse(SDL_MOUSEBUTTONUP, x, y, false);
		return true;
	}

	int PlantsAt(Board* theBoard, int theCol, int theRow)
	{
		int aCount = 0;
		for (Plant* aPlant : theBoard->mPlants)
			if (!aPlant->mDead && aPlant->mPlantCol == theCol && aPlant->mRow == theRow)
				aCount++;
		return aCount;
	}

	void RunTest(uint32_t theNow)
	{
		static const char* sTest = SDL_getenv("PVZ_AR_DIORAMA_TEST");
		if (!sTest || !*sTest || SDL_strcmp(sTest, "0") == 0)
			return;
		static uint32_t sStart = 0;		// when the lawn is busy enough for the shots
		static uint32_t sStep = 0;
		static uint32_t sStepAt = 0;
		static int sSunBefore = 0;
		static int sPlantCol = -1, sPlantRow = -1;
		static int sPassed = 0, sFailed = 0;
		static float sDragYaw = 0;
		static unsigned int sDragPlants = 0;	// plants before the drag, which mustn't plant
		static int sPlantsBefore = 0;
		static unsigned int sCoinId = 0;
		static int sCoinType = -1;
		Board* aBoard = gLawnApp->mBoard;
		if (!aBoard)
			return;
		if (sStart == 0)
		{
			static uint32_t sFirst = theNow;
			int aZombies = 0;
			for (Zombie* aZombie : aBoard->mZombies)
				if (!aZombie->mDead && aZombie->mPosX < 760)
					aZombies++;
			if (aZombies < 3 && theNow - sFirst < 45000)
				return;
			sStart = theNow;
			sStepAt = theNow;
			PvzpLogLn("ARDiorama test: {} zombies on the lawn, starting", aZombies);
		}
		if (theNow < sStepAt)
			return;
		constexpr int VIEWS = sizeof(TEST_VIEWS) / sizeof(TEST_VIEWS[0]);
		auto Next = [&](uint32_t theDelay) {
			sStep++;
			sStepAt = theNow + theDelay;
		};
		auto Check = [&](const char* theWhat, bool theOk) {
			(theOk ? sPassed : sFailed)++;
			PvzpLogLn("ARDiorama test: {} {}", theOk ? "PASS" : "FAIL", theWhat);
		};
		auto SetView = [&](float theYaw, float thePitch, float theDist) {
			gSpin = 0;
			gOrbit.mYaw = theYaw * DEG;
			gOrbit.mPitch = thePitch * DEG;
			gOrbit.mDist = theDist;
		};
		if (sStep < static_cast<uint32_t>(VIEWS) * 2)
		{
			// Each view, then its shot a frame later (the cards turn toward the camera of the frame drawn)
			const TestView& v = TEST_VIEWS[sStep / 2];
			if (sStep % 2 == 0)
				SetView(v.mYaw, v.mPitch, v.mDist);
			else
				Shot(v.mName);
			Next(sStep % 2 == 0 ? 150 : 350);
			return;
		}
		uint32_t aStep = sStep - VIEWS * 2;
		if (SDL_strcmp(sTest, "shots") == 0)
			aStep = 100;
		switch (aStep)
		{
		case 0:
			// A sun that pops out of a plant and lands on the lawn
			SetView(0, 55, 0.62f);
			aBoard->AddCoin(470, 260, CoinType::COIN_SUN, CoinMotion::COIN_MOTION_FROM_PLANT);
			Next(1800);
			break;
		case 1:
		{
			sSunBefore = aBoard->mSunMoney;
			float p[3];
			int cx = 0, cy = 0;
			sCoinId = 0;
			if (ARDiorama::CoinPoint(p, &cx, &cy))
			{
				for (Coin* aCoin : aBoard->mCoins)
					if (!aCoin->mDead && cx >= aCoin->mPosX && cx < aCoin->mPosX + aCoin->mWidth && cy >= aCoin->mPosY && cy < aCoin->mPosY + aCoin->mHeight)
					{
						sCoinId = aBoard->mCoins.DataArrayGetID(aCoin);
						sCoinType = static_cast<int>(aCoin->mType);
					}
				ClickPoint("sun", p, cx, cy);
			}
			else
				PvzpLogLn("ARDiorama test: no sun card");
			Shot("pick-sol");
			Next(2500);
			break;
		}
		case 2:
		{
			// The sun, or a coin a zombie dropped if that card came first: collected either way
			Coin* aCoin = sCoinId ? aBoard->mCoins.DataArrayTryToGet(sCoinId) : nullptr;
			bool aCollected = aBoard->mSunMoney > sSunBefore || (sCoinId && (!aCoin || aCoin->mDead || aCoin->mIsBeingCollected));
			Check(std::format("sun or coin collected (coin type {}, sun {} -> {})", sCoinType, sSunBefore, aBoard->mSunMoney).c_str(), aCollected);
			aBoard->AddSunMoney(1000);
			Next(300);
			break;
		}
		case 3:
		{
			// The first seed packet with a free cell near the front, where nothing stands between it and the camera,
			// through the seed bank panel
			SeedBank* aBank = aBoard->mSeedBank.get();
			int aPacketIndex = -1;
			sPlantCol = sPlantRow = -1;
			for (int i = 0; aBank && i < aBank->mNumPackets; i++)
				if (!aBank->mSeedPackets[i].mActive)
					aBank->mSeedPackets[i].Activate();	// the defense the test planted may have left them recharging
			for (int aTry = 0; aTry < 2 && aPacketIndex < 0; aTry++)
			{
				for (int i = 0; aBank && i < aBank->mNumPackets && aPacketIndex < 0; i++)
					for (int aRow = aBoard->StageHasPool() ? 5 : 4; aRow >= 0 && aPacketIndex < 0; aRow--)
						for (int aCol = 8; aCol >= 0 && aPacketIndex < 0; aCol--)
							if (aBoard->CanPlantAt(aCol, aRow, aBank->mSeedPackets[i].mPacketType) == PlantingReason::PLANTING_OK && aBank->mSeedPackets[i].CanPickUp())
							{
								aPacketIndex = i;
								sPlantCol = aCol;
								sPlantRow = aRow;
							}
				// On the roof without a flower pot in the bank every pot may be taken: one more, to plant on
				if (aPacketIndex < 0 && aTry == 0 && aBoard->StageHasRoof() && aBoard->CanPlantAt(7, 4, SeedType::SEED_FLOWERPOT) == PlantingReason::PLANTING_OK)
					aBoard->AddPlant(7, 4, SeedType::SEED_FLOWERPOT);
			}
			if (aPacketIndex < 0)
			{
				PvzpLogLn("ARDiorama test: no seed packet with a free cell");
				Next(0);
				break;
			}
			const SeedPacket& aPacket = aBank->mSeedPackets[aPacketIndex];
			int gx = aBank->mX + aPacket.mX + aPacket.mWidth / 2, gy = aBank->mY + aPacket.mY + aPacket.mHeight / 2;
			float p[3];
			if (ARDiorama::PanelPoint(gx, gy, p))
				ClickPoint("seed packet", p, gx, gy);
			else
				PvzpLogLn("ARDiorama test: the packet at {},{} is not on the panel", gx, gy);
			Next(500);
			break;
		}
		case 4:
		{
			CursorObject* aCursor = aBoard->mCursorObject.get();
			bool aHolding = aCursor && aCursor->mCursorType == CursorType::CURSOR_TYPE_PLANT_FROM_BANK;
			Check(std::format("seed picked from the bank (cursor {})", aCursor ? static_cast<int>(aCursor->mCursorType) : -1).c_str(), aHolding);
			Shot("pick-sobre");
			if (aHolding && sPlantCol >= 0)
			{
				int gx = aBoard->GridToPixelX(sPlantCol, sPlantRow) + 40, gy = aBoard->GridToPixelY(sPlantCol, sPlantRow) + 40;
				float p[3];
				ARDiorama::LawnPoint(gx, gy, p);
				sPlantsBefore = PlantsAt(aBoard, sPlantCol, sPlantRow);
				sSunBefore = aBoard->mSunMoney;
				ClickPoint("lawn cell", p, gx, gy);
			}
			else
				PvzpLogLn("ARDiorama test: no free cell for the seed");
			Next(900);
			break;
		}
		case 5:
		{
			// One more plant on that cell (a pot on the roof is one already; elsewhere the defense may be eaten meanwhile)
			Plant* aPlant = sPlantCol >= 0 ? aBoard->GetTopPlantAt(sPlantCol, sPlantRow, PlantPriority::TOPPLANT_ANY) : nullptr;
			CursorObject* aCursor = aBoard->mCursorObject.get();
			int aPlants = sPlantCol >= 0 ? PlantsAt(aBoard, sPlantCol, sPlantRow) : 0;
			Check(std::format("planted at column {} row {} (plants there {} -> {}, top type {}, sun {} -> {}, cursor {})", sPlantCol, sPlantRow, sPlantsBefore,
				aPlants, aPlant ? static_cast<int>(aPlant->mSeedType) : -1, sSunBefore, aBoard->mSunMoney, aCursor ? static_cast<int>(aCursor->mCursorType) : -1).c_str(),
				aPlants > sPlantsBefore);
			Shot("pick-planta");
			Next(1500);
			break;
		}
		case 6:
		{
			Shot("pick-despues");
			sDragYaw = gOrbit.mYaw;
			sDragPlants = aBoard->mPlants.mSize;
			PushMouse(SDL_MOUSEBUTTONDOWN, 300, 300, true);
			for (int i = 1; i <= 5; i++)
				PushMouse(SDL_MOUSEMOTION, 300 + i * 30, 300, true);
			PushMouse(SDL_MOUSEBUTTONUP, 450, 300, false);
			Next(500);
			break;
		}
		case 7:
			Check(std::format("a drag turns the camera ({:.0f} -> {:.0f} degrees) without clicking", sDragYaw / DEG, gOrbit.mYaw / DEG).c_str(),
				std::fabs(gOrbit.mYaw - sDragYaw) > 0.5f && aBoard->mPlants.mSize == sDragPlants);
			PvzpLogLn("ARDiorama test: done, {} passed, {} failed", sPassed, sFailed);
			Next(1500);
			break;
		case 8:
		case 100:
			if (SDL_getenv("PVZ_AR_DIORAMA_TEST_STAY") == nullptr)
				gLawnApp->Shutdown();
			Next(1000000);
			break;
		default:
			break;
		}
	}
}

void ARDioramaPreview::OnFrame()
{
	static const bool sForced = [] {
		const char* aEnv = SDL_getenv("PVZ_AR_DIORAMA");
		return aEnv && *aEnv && SDL_strcmp(aEnv, "0") != 0;
	}();
	static bool sInited = false;
	if (!sInited)
	{
		sInited = true;
		if (const char* aCam = SDL_getenv("PVZ_AR_DIORAMA_CAM"))	// yaw,pitch in degrees, distance in meters
		{
			float aYaw = 0, aPitch = 38, aDist = 0.62f;
			if (SDL_sscanf(aCam, "%f,%f,%f", &aYaw, &aPitch, &aDist) == 3)
				gOrbit = { aYaw * DEG, aPitch * DEG, aDist };
		}
		if (const char* aSpin = SDL_getenv("PVZ_AR_DIORAMA_ORBIT"))
			gSpin = static_cast<float>(SDL_atof(aSpin));
	}
	uint32_t aNow = SDL_GetTicks();
	bool aWanted = gLawnApp && gLawnApp->mWindow && (sForced || Mods::Settings().mARDiorama);
	if (!aWanted)
	{
		gShowing = false;
		gPressed = false;
		return;
	}
	ARDiorama::Update(gLawnApp, aNow, UPDATE_INTERVAL_MS);
	gShowing = ARDiorama::HasScene();
	if (!gShowing)
	{
		gPressed = false;
		gLastFrame = 0;
		return;
	}
	if (gLastFrame != 0 && gSpin != 0)
		gOrbit.mYaw += gSpin * DEG * (aNow - gLastFrame) / 1000.0f;
	gLastFrame = aNow;

	int aWidth = 0, aHeight = 0;
	SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
	PlaceCamera(aWidth, aHeight);
	RunTest(aNow);
	PlaceCamera(aWidth, aHeight);

	// The whole window, over the game's picture: a dark room, a table and the board on it
	GLboolean aDepthMask = GL_TRUE, aScissor = glIsEnabled(GL_SCISSOR_TEST);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &aDepthMask);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, aWidth, aHeight);
	glClearColor(0x1d / 255.0f, 0x19 / 255.0f, 0x15 / 255.0f, 1);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0, 0, 0, 1);
	ARDiorama::DrawTable(gView, gProj, TABLE_RADIUS);
	ARDiorama::Draw(gModel, gView, gProj);
	glDepthMask(aDepthMask);
	if (aScissor)
		glEnable(GL_SCISSOR_TEST);
	const Sexy::Rect& aRect = gLawnApp->mGLInterface->mPresentationRect;
	glViewport(aRect.mX, aRect.mY, aRect.mWidth, aRect.mHeight);
}

bool ARDioramaPreview::ProcessEvent(const SDL_Event& theEvent)
{
	if (!gShowing || !gLawnApp)
		return false;
	switch (theEvent.type)
	{
	case SDL_MOUSEBUTTONDOWN:
		if (theEvent.button.button == SDL_BUTTON_LEFT)
		{
			gPressed = true;
			gMoved = false;
			gDownX = gLastX = theEvent.button.x;
			gDownY = gLastY = theEvent.button.y;
		}
		return true;
	case SDL_MOUSEMOTION:
		if (gPressed)
		{
			int dx = theEvent.motion.x - gLastX, dy = theEvent.motion.y - gLastY;
			gLastX = theEvent.motion.x;
			gLastY = theEvent.motion.y;
			if (std::abs(gLastX - gDownX) + std::abs(gLastY - gDownY) > 6)
				gMoved = true;
			if (gMoved)
			{
				gSpin = 0;
				gOrbit.mYaw -= dx * 0.008f;
				gOrbit.mPitch += dy * 0.006f;
			}
		}
		return true;
	case SDL_MOUSEBUTTONUP:
		if (theEvent.button.button == SDL_BUTTON_LEFT && gPressed)
		{
			gPressed = false;
			if (!gMoved)
				Click(theEvent.button.x, theEvent.button.y);
		}
		return true;
	case SDL_MOUSEWHEEL:
		gOrbit.mDist *= std::exp(-0.1f * theEvent.wheel.y);
		return true;
	case SDL_MULTIGESTURE:
		gOrbit.mDist *= std::max(0.5f, 1 - theEvent.mgesture.dDist * 3);
		return false;
	default:
		return false;
	}
}
