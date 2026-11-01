#ifndef __ARDIORAMAPREVIEW_H__
#define __ARDIORAMAPREVIEW_H__

union SDL_Event;

// "Diorama 3D en la ventana": the game's own window shows ARDiorama while a lawn is in play, with a camera orbiting
// the board on a table (drag to turn, wheel to zoom) and clicks going through ARDiorama::Pick, as the phone's taps
// will. To try the AR drawing without a phone, and a 3D view on the desktop. On while the hack menu option or
// PVZ_AR_DIORAMA=1 is on.
namespace ARDioramaPreview
{
	void	OnFrame();								// on the GL thread after the game's draw (HackMenu::Render)
	bool	ProcessEvent(const SDL_Event& theEvent);	// a mouse event the preview used
}

#endif
