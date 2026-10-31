#ifndef __ARVIEW_H__
#define __ARVIEW_H__

#include <string>

// "Ver en AR": the game served to a phone browser that lays it on a real table with WebXR (the /ar page,
// ARViewPage.h) and plays it with taps. Runs on any screen, menus included, while the hack menu option or
// PVZ_AR_VIEW=1 is on, on port 8092 (PVZ_AR_PORT) with its own code (kept in the registry, PVZ_AR_CODE pins
// it for tests). The page's taps come back as mouse events, handed to the widgets on the game thread.
namespace ARView
{
	void		OnFrame();			// on the GL thread after the game's draw (HackMenu::Render)
	bool		DispatchMouse();	// SexyAppBase::ProcessDeferredMessages: one queued tap event to the widgets, true if there was one
	std::string	HintText();			// where to open the page (or why it can't listen), "" when off
	bool		HasViewer();		// a phone is watching
}

#endif
