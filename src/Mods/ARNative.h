#ifndef __ARNATIVE_H__
#define __ARNATIVE_H__

#include <string>

union SDL_Event;

// "Ver en AR en este cel": the lawn as a diorama on a real table seen through this phone's camera, with ARCore's
// C API inside the game (no browser, no other computer). While the hack menu option is on, the camera fills the
// screen during a lawn in play (ARDiorama draws the board on its anchor) and the 2D game shows everywhere else,
// with the camera on its sides. Android builds with ARCore only (PVZ_AR_NATIVE); elsewhere these do nothing.
namespace ARNative
{
#ifdef PVZ_AR_NATIVE
	void		OnFrame();												// GL thread, after the game's draw and before ImGui's
	bool		ProcessEvent(const SDL_Event& theEvent, bool theTouchOnMenu);	// a touch not on ImGui; true when AR used it
	void		CancelTouch();											// the fingers so far were the menu's 3-finger toggle
	bool		OverlayShown();											// its ImGui controls or a notice are on screen
	void		DrawOverlay(float theScale);							// inside the ImGui frame
	std::string	StatusText();											// for the hack menu, "" when off
	constexpr bool Available() { return true; }
#else
	inline void			OnFrame() {}
	inline bool			ProcessEvent(const SDL_Event&, bool) { return false; }
	inline void			CancelTouch() {}
	inline bool			OverlayShown() { return false; }
	inline void			DrawOverlay(float) {}
	inline std::string	StatusText() { return ""; }
	constexpr bool		Available() { return false; }
#endif
}

#endif
