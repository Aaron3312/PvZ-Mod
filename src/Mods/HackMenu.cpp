#include <SDL.h>
#include "HackMenu.h"
#include "VersusNet.h"
#include "Mods.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include "imgui.h"
#include "imgui_internal.h"	// window list and active id, to route touches (see TouchOnMenu)
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "SexyAppFramework/imagelib/ImageLib.h"
#include <algorithm>
#include <memory>
#include <string>

namespace
{
	bool gReady = false;
	bool gOpen = false;
	float gScale = 1.0f;			// menu scale: 1 on desktop, from the screen density on Android
	bool gTouchOnMenu = false;		// the finger now on the screen went down on the menu
	std::string gScreenshotPath;	// pending RequestScreenshot

	// Reads the default framebuffer (RGBA, bottom-up) into the engine's BGRA top-down image and writes a PNG.
	void SaveFramebuffer(const std::string& thePath)
	{
		int aWidth = 0, aHeight = 0;
		SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
		if (aWidth <= 0 || aHeight <= 0)
			return;
		auto aPixels = std::make_unique<uint8_t[]>(static_cast<size_t>(aWidth) * aHeight * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, aWidth, aHeight, GL_RGBA, GL_UNSIGNED_BYTE, aPixels.get());

		ImageLib::Image anImage;
		anImage.mWidth = aWidth;
		anImage.mHeight = aHeight;
		anImage.mBits = std::make_unique<uint32_t[]>(static_cast<size_t>(aWidth) * aHeight);
		for (int y = 0; y < aHeight; y++)
		{
			const uint8_t* aSrc = aPixels.get() + static_cast<size_t>(aHeight - 1 - y) * aWidth * 4;
			uint32_t* aDst = anImage.mBits.get() + static_cast<size_t>(y) * aWidth;
			for (int x = 0; x < aWidth; x++, aSrc += 4)
				aDst[x] = 0xFF000000u | (aSrc[0] << 16) | (aSrc[1] << 8) | aSrc[2];
		}
		ImageLib::WritePNGImage(thePath, &anImage);
	}
	int gSpawnIndex = 0;

	struct SpawnChoice
	{
		ZombieType	mType;
		const char*	mName;
	};

	// The boss and the Wall-nut/Peashooter heads only work in their own modes.
	const SpawnChoice gSpawnChoices[] = {
		{ ZombieType::ZOMBIE_NORMAL, "Zombi" },
		{ ZombieType::ZOMBIE_FLAG, "Zombi abanderado" },
		{ ZombieType::ZOMBIE_TRAFFIC_CONE, "Caracono" },
		{ ZombieType::ZOMBIE_POLEVAULTER, "Saltador con pertiga" },
		{ ZombieType::ZOMBIE_PAIL, "Caracubo" },
		{ ZombieType::ZOMBIE_NEWSPAPER, "Zombi del periodico" },
		{ ZombieType::ZOMBIE_DOOR, "Zombi con puerta" },
		{ ZombieType::ZOMBIE_FOOTBALL, "Zombi futbolista" },
		{ ZombieType::ZOMBIE_DANCER, "Zombi bailarin" },
		{ ZombieType::ZOMBIE_DUCKY_TUBE, "Zombi con flotador" },
		{ ZombieType::ZOMBIE_SNORKEL, "Zombi buceador" },
		{ ZombieType::ZOMBIE_ZAMBONI, "Zomboni" },
		{ ZombieType::ZOMBIE_BOBSLED, "Equipo de bobsleigh" },
		{ ZombieType::ZOMBIE_DOLPHIN_RIDER, "Jinete de delfin" },
		{ ZombieType::ZOMBIE_JACK_IN_THE_BOX, "Zombi caja sorpresa" },
		{ ZombieType::ZOMBIE_BALLOON, "Zombi globo" },
		{ ZombieType::ZOMBIE_DIGGER, "Zombi minero" },
		{ ZombieType::ZOMBIE_POGO, "Zombi saltarin" },
		{ ZombieType::ZOMBIE_YETI, "Zombi yeti" },
		{ ZombieType::ZOMBIE_BUNGEE, "Zombi puenting" },
		{ ZombieType::ZOMBIE_LADDER, "Zombi escalera" },
		{ ZombieType::ZOMBIE_CATAPULT, "Zombi catapulta" },
		{ ZombieType::ZOMBIE_GARGANTUAR, "Gargantua" },
		{ ZombieType::ZOMBIE_IMP, "Diablillo" },
		{ ZombieType::ZOMBIE_REDEYE_GARGANTUAR, "Gargantua de ojos rojos" },
		{ ZombieType::ZOMBIE_SPEEDY, "Zombi veloz (mod)" },
		{ ZombieType::ZOMBIE_GIANT_PAIL, "Caracubo gigante (mod)" },
		{ ZombieType::ZOMBIE_ICE, "Zombi de hielo (mod)" },
		{ ZombieType::ZOMBIE_GHOST, "Zombi fantasma (mod)" },
		{ ZombieType::ZOMBIE_HEALER, "Zombi medico (mod)" },
		{ ZombieType::ZOMBIE_KAMIKAZE, "Zombi kamikaze (mod)" },
		{ ZombieType::ZOMBIE_SPLITTER, "Zombi divisor (mod)" },
		{ ZombieType::ZOMBIE_MINI, "Minizombi (mod)" },
		{ ZombieType::ZOMBIE_HOPPER, "Zombi saltacarril (mod)" },
	};

	bool IsToggleKey(const SDL_Event& theEvent)
	{
		if (theEvent.type != SDL_KEYDOWN || theEvent.key.repeat)
			return false;
		// GRAVE and NONUSBACKSLASH swap places on ISO keyboards (Spanish Mac: the key left of "1").
		SDL_Scancode aCode = theEvent.key.keysym.scancode;
		return aCode == SDL_SCANCODE_GRAVE || aCode == SDL_SCANCODE_NONUSBACKSLASH || aCode == SDL_SCANCODE_F1;
	}

	// Phones have no keyboard: three fingers on the screen within a moment toggle the menu.
	// Only touch screens count; macOS also sends finger events for the trackpad (Mission Control swipes).
	// The fingers are counted from the events, which may lag behind SDL's live finger list.
	bool IsToggleTouch(const SDL_Event& theEvent)
	{
		static int sDown = 0;			// fingers put down in this gesture
		static Uint32 sFirstTick = 0;	// when the first of them went down
		static bool sFired = false;		// one toggle per gesture
		if ((theEvent.type != SDL_FINGERDOWN && theEvent.type != SDL_FINGERUP) ||
			SDL_GetTouchDeviceType(theEvent.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT)
			return false;

		int aLiveFingers = SDL_GetNumTouchFingers(theEvent.tfinger.touchId);
		if (theEvent.type == SDL_FINGERUP)
		{
			if (sDown > 0)
				sDown--;
			if (sDown == 0 || aLiveFingers <= 0)
			{
				sDown = 0;
				sFired = false;
			}
			return false;
		}

		if (sDown == 0 || aLiveFingers <= 1)	// a new gesture (also recovers from a lost FINGERUP)
		{
			sDown = 0;
			sFired = false;
			sFirstTick = theEvent.tfinger.timestamp;
		}
		sDown++;
		if (sFired || sDown < 3 || theEvent.tfinger.timestamp - sFirstTick > 800)
			return false;
		sFired = true;
		return true;
	}

	// Touch sends the motion and the press in the same batch, before ImGui's next frame can update
	// WantCaptureMouse. So a touch goes to whoever is under the finger when it goes down, and the
	// rest of that touch (drag, release) follows it. Uses last frame's windows, popups included.
	bool TouchOnMenu(int theX, int theY)
	{
		const ImGuiContext& g = *ImGui::GetCurrentContext();
		ImVec2 aPos(static_cast<float>(theX), static_cast<float>(theY));
		for (const ImGuiWindow* aWindow : g.Windows)
			if (aWindow->Active && !aWindow->Hidden && aWindow->Rect().Contains(aPos))
				return true;
		return false;
	}

	// Android: scale the menu with the screen density (160 dpi = 1x) so text stays readable and
	// widgets big enough for a finger: a 450 dpi phone gets x2.5, and the whole menu (about 250
	// pixels high at x1) still fits a 1080-pixel-high screen. Desktop keeps x1;
	// PVZ_HACK_MENU_SCALE=<x> forces a scale (testing).
	float MenuScale()
	{
		if (const char* aForced = SDL_getenv("PVZ_HACK_MENU_SCALE"))
			return std::clamp(static_cast<float>(SDL_atof(aForced)), 1.0f, 4.0f);
#ifdef __ANDROID__
		SDL_Window* aWindow = static_cast<SDL_Window*>(gLawnApp->mWindow);
		float aDpi = 0.0f;
		if (SDL_GetDisplayDPI(std::max(SDL_GetWindowDisplayIndex(aWindow), 0), &aDpi, nullptr, nullptr) == 0 && aDpi > 0.0f)
			return std::clamp(aDpi / 160.0f * 0.9f, 1.0f, 4.0f);
		int aWidth = 0, aHeight = 0;
		SDL_GL_GetDrawableSize(aWindow, &aWidth, &aHeight);
		return std::clamp(std::min(aWidth, aHeight) / 540.0f, 1.0f, 4.0f);
#else
		return 1.0f;
#endif
	}

	bool EnsureReady()
	{
		if (gReady)
			return true;
		if (!gLawnApp || !gLawnApp->mWindow || !gLawnApp->mContext)
			return false;
		// The shaders below target GL 2.1 (macOS fallback) or GLES 2.0.
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		ImGui::StyleColorsDark();
		ImGuiStyle& aStyle = ImGui::GetStyle();
		aStyle.FontSizeBase = 17.0f;
		gScale = MenuScale();
		if (gScale != 1.0f)
		{
			aStyle.ScaleAllSizes(gScale);
			aStyle.FontScaleDpi = gScale;
			aStyle.ScrollbarSize *= 1.5f;	// dragged with a finger
			aStyle.TouchExtraPadding = ImVec2(3.0f * gScale, 3.0f * gScale);
			io.ConfigWindowsMoveFromTitleBarOnly = true;	// dragging the body scrolls it (DrawMenu)
		}
#ifdef __ANDROID__
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
#endif
		ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(gLawnApp->mWindow), gLawnApp->mContext);
		ImGui_ImplOpenGL3_Init(gDesktopGLFallback ? "#version 120" : "#version 100");
		Mods::Load(gLawnApp);
		if (SDL_getenv("PVZ_HACK_MENU_OPEN"))
			gOpen = true;	// start with the menu open (handy for testing without a keyboard)
		gReady = true;
		return true;
	}

	bool IsPlaying()
	{
		return gLawnApp->mBoard && gLawnApp->mGameScene == GameScenes::SCENE_PLAYING;
	}

	void WinLevel()
	{
		// Same path as the '!' debug key, which handles survival, last stand and puzzle stages.
		bool aDebugKeys = gLawnApp->mDebugKeysEnabled;
		gLawnApp->mDebugKeysEnabled = true;
		gLawnApp->mBoard->KeyChar('!');
		gLawnApp->mDebugKeysEnabled = aDebugKeys;
	}

	void DrawMenu()
	{
		ModSettings& s = Mods::Settings();
		bool aChanged = false;
		Board* aBoard = gLawnApp->mBoard;

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(16 * gScale, 16 * gScale), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(std::min(560 * gScale, io.DisplaySize.x - 32 * gScale), 0), ImGuiCond_FirstUseEver);
		if (gScale != 1.0f)	// scaled up it may not fit: keep it on screen and let it scroll
			ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x - 32 * gScale, io.DisplaySize.y - 32 * gScale));
#ifdef __ANDROID__
		const char* aTitle = "Trucos  (3 dedos para cerrar)";
#else
		const char* aTitle = "Trucos  (\xc2\xba / fn+F1 para cerrar)";
#endif
		if (!ImGui::Begin(aTitle, &gOpen))
		{
			ImGui::End();
			return;
		}
		if (gScale != 1.0f)
		{
			// No wheel on a phone: dragging the window body scrolls it (the title bar still moves it).
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			if (g.ActiveId == ImGui::GetCurrentWindow()->MoveId && !g.MovingWindow && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
				ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
		}
		ImGui::PushItemWidth(ImGui::GetFontSize() * 12);

		if (ImGui::CollapsingHeader("Sol y plantas", ImGuiTreeNodeFlags_DefaultOpen))
		{
			aChanged |= ImGui::Checkbox("Sol infinito", &s.mInfiniteSun);
			ImGui::SameLine();
			ImGui::BeginDisabled(!aBoard);
			if (ImGui::Button("+1000 sol"))
				aBoard->AddSunMoney(1000);
			ImGui::EndDisabled();
			aChanged |= ImGui::Checkbox("Sin recarga", &s.mNoRecharge);
			aChanged |= ImGui::SliderInt("Precio (%)", &s.mCostPercent, 0, 100);
			aChanged |= ImGui::Checkbox("Plantar sin reglas", &s.mEasyPlanting);
			ImGui::SetItemTooltip("Gratis, sin recarga y mejoras en cualquier casilla");
			aChanged |= ImGui::Checkbox("Plantas invencibles", &s.mInvinciblePlants);
		}

		if (ImGui::CollapsingHeader("Combate", ImGuiTreeNodeFlags_DefaultOpen))
		{
			aChanged |= ImGui::SliderInt("Dano de plantas (%)", &s.mDamagePercent, 10, 2000);
			aChanged |= ImGui::Checkbox("Matar de un golpe", &s.mOneHitKill);
			aChanged |= ImGui::SliderInt("Vida de zombis nuevos (%)", &s.mZombieHealthPercent, 10, 1000);
			aChanged |= ImGui::Checkbox("Zombis congelados", &s.mFreezeZombies);

			ImGui::BeginDisabled(!IsPlaying());
			if (ImGui::Button("Matar todos los zombis"))
				aBoard->RemoveAllZombies();
			ImGui::SetNextItemWidth(220 * gScale);
			ImGui::Combo("##zombi", &gSpawnIndex, [](void*, int i) { return gSpawnChoices[i].mName; }, nullptr, IM_ARRAYSIZE(gSpawnChoices));
			ImGui::SameLine();
			if (ImGui::Button("Invocar"))
				aBoard->AddZombie(gSpawnChoices[gSpawnIndex].mType, Zombie::ZOMBIE_WAVE_DEBUG);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Partida", ImGuiTreeNodeFlags_DefaultOpen))
		{
			aChanged |= ImGui::SliderInt("Velocidad", &s.mSpeed, 1, 10, "x%d");
			aChanged |= ImGui::Checkbox("Dinero infinito ($999,990)", &s.mInfiniteCoins);
			ImGui::BeginDisabled(!IsPlaying());
			if (ImGui::Button("Ganar el nivel"))
				WinLevel();
			ImGui::SameLine();
			if (ImGui::Button("Elegir nivel..."))
				gLawnApp->DoCheatDialog();
			ImGui::EndDisabled();
		}

		ImGui::PopItemWidth();
		if (aChanged)
			Mods::Save(gLawnApp);

		ImGui::End();
	}
}

bool HackMenu::ProcessEvent(const SDL_Event& theEvent)
{
	if (IsToggleKey(theEvent) || IsToggleTouch(theEvent))
	{
		gOpen = !gOpen;
		return true;
	}
	if (!gReady || !gOpen)
	{
		// A touch that went down on the menu stays the menu's until it ends, even if the menu closed.
		bool aTouchMouse = (theEvent.type == SDL_MOUSEMOTION && theEvent.motion.which == SDL_TOUCH_MOUSEID) ||
			((theEvent.type == SDL_MOUSEBUTTONDOWN || theEvent.type == SDL_MOUSEBUTTONUP) && theEvent.button.which == SDL_TOUCH_MOUSEID);
		if (!gTouchOnMenu || !aTouchMouse)
			return false;
		if (theEvent.type != SDL_MOUSEMOTION)
			gTouchOnMenu = false;
		return true;
	}

	ImGui_ImplSDL2_ProcessEvent(&theEvent);
	const ImGuiIO& io = ImGui::GetIO();
	switch (theEvent.type)
	{
	case SDL_MOUSEMOTION:
		if (theEvent.motion.which == SDL_TOUCH_MOUSEID)
			return (theEvent.motion.state & SDL_BUTTON_LMASK) ? gTouchOnMenu : TouchOnMenu(theEvent.motion.x, theEvent.motion.y);
		return io.WantCaptureMouse;
	case SDL_MOUSEBUTTONDOWN:
		if (theEvent.button.which == SDL_TOUCH_MOUSEID)
			return gTouchOnMenu = TouchOnMenu(theEvent.button.x, theEvent.button.y);
		return io.WantCaptureMouse;
	case SDL_MOUSEBUTTONUP:
		if (theEvent.button.which == SDL_TOUCH_MOUSEID)
		{
			bool aOnMenu = gTouchOnMenu;
			gTouchOnMenu = false;
			return aOnMenu;
		}
		return io.WantCaptureMouse;
	case SDL_MOUSEWHEEL:
		return io.WantCaptureMouse;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
	case SDL_TEXTINPUT:
		return io.WantCaptureKeyboard;
	default:
		return false;
	}
}

void HackMenu::RequestScreenshot(const char* thePngPath)
{
	gScreenshotPath = thePngPath;
}

void HackMenu::Render()
{
	VersusNet::OnFrame();	// before the menu is drawn, so the other computer doesn't see it

	if (!gScreenshotPath.empty() && gLawnApp && gLawnApp->mWindow)
	{
		SaveFramebuffer(gScreenshotPath);
		gScreenshotPath.clear();
	}

	// The first frame creates the context and loads the saved mods, menu open or not.
	if (!gReady && !EnsureReady())
		return;
	if (!gOpen)
		return;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	DrawMenu();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// PVZ_HACK_MENU_SHOT=<png>: saves one frame with the menu on it, to check its layout (e.g. with PVZ_HACK_MENU_SCALE).
	static int sMenuFrames = 0;
	if (++sMenuFrames == 60)
		if (const char* aShot = SDL_getenv("PVZ_HACK_MENU_SHOT"))
			SaveFramebuffer(aShot);
}
