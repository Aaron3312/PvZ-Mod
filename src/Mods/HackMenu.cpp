#include <SDL.h>
#include "HackMenu.h"
#include "Mods.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "SexyAppFramework/imagelib/ImageLib.h"
#include <memory>
#include <string>

namespace
{
	bool gReady = false;
	bool gOpen = false;
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
	};

	bool IsToggleKey(const SDL_Event& theEvent)
	{
		if (theEvent.type != SDL_KEYDOWN || theEvent.key.repeat)
			return false;
		// GRAVE and NONUSBACKSLASH swap places on ISO keyboards (Spanish Mac: the key left of "1").
		SDL_Scancode aCode = theEvent.key.keysym.scancode;
		return aCode == SDL_SCANCODE_GRAVE || aCode == SDL_SCANCODE_NONUSBACKSLASH || aCode == SDL_SCANCODE_F1;
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
		ImGui::GetStyle().FontSizeBase = 17.0f;
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

		ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Trucos  (\xc2\xba / fn+F1 para cerrar)", &gOpen))
		{
			ImGui::End();
			return;
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
			ImGui::SetNextItemWidth(220);
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
	if (IsToggleKey(theEvent))
	{
		gOpen = !gOpen;
		return true;
	}
	if (!gReady || !gOpen)
		return false;

	ImGui_ImplSDL2_ProcessEvent(&theEvent);
	const ImGuiIO& io = ImGui::GetIO();
	switch (theEvent.type)
	{
	case SDL_MOUSEMOTION:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
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
}
