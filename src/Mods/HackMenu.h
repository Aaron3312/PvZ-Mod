#ifndef __HACKMENU_H__
#define __HACKMENU_H__

union SDL_Event;

// In-game cheat menu drawn with Dear ImGui on top of the game.
// Opens and closes with the key left of "1" (º / `) or F1, or on a touch screen with three fingers at once.
namespace HackMenu
{
	bool	ProcessEvent(const SDL_Event& theEvent);	// true when the menu used the event
	void	Render();									// call after the game's draw flush, before the swap
	void	RequestScreenshot(const char* thePngPath);	// saves the next frame (without the menu) as PNG
}

#endif
