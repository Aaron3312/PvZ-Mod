#include "ARView.h"
#include "Mods.h"
#include "HackMenu.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Coin.h"
#include "Lawn/GridItem.h"
#include "Lawn/LawnMower.h"
#include "Lawn/MessageWidget.h"
#include "Lawn/Plant.h"
#include "Lawn/Projectile.h"
#include "Lawn/Zombie.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLImage.h"
#include "SexyAppFramework/graphics/GLInterface.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include "SexyAppFramework/graphics/Graphics.h"
#include "SexyAppFramework/widget/WidgetManager.h"
#include <SDL.h>

#ifdef _WIN32

void ARView::OnFrame()
{
}

bool ARView::DispatchMouse()
{
	return false;
}

std::string ARView::HintText()
{
	return "";
}

bool ARView::HasViewer()
{
	return false;
}

bool ARView::DrawingFigures()
{
	return false;
}

#else

#include "ARViewPage.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>
#include <jpeglib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// The server is VersusNet's, trimmed and with its own state, so both can run at once and neither file
// depends on the other.
namespace
{
	constexpr int		DEFAULT_PORT = 8092;
	constexpr int		FRAME_INTERVAL_MS = 60;		// about 16 frames a second for the board's texture
	constexpr int		MIN_FRAME_WIDTH = 800;		// drawables are scaled down by a whole factor, keeping at least this
	constexpr int		JPEG_QUALITY = 80;			// the board is seen up close
	constexpr int		MAX_VIEWERS = 6;
	constexpr size_t	MAX_QUEUED_MOUSE = 64;
	constexpr uint32_t	POLL_KEEPS_MS = 2000;		// a /frame.jpg keeps the capture going this long
	constexpr uint32_t	RETRY_LISTEN_MS = 5000;
	constexpr uint32_t	SHOT_DELAY_MS = 700;

	// The diorama (/scene): while a lawn is in play every object is drawn alone into an atlas, so the page can stand
	// it up on a 3D lawn. Colors (premultiplied) and alpha go side by side in one JPEG.
	constexpr uint32_t	SCENE_INTERVAL_MS = 85;		// about 11 updates a second
	constexpr int		ATLAS_WIDTH = 1024;
	constexpr int		ATLAS_MAX_HEIGHT = 2048;
	constexpr int		ATLAS_PAD = 3;
	constexpr int		ATLAS_HEIGHT_STEP = 64;		// fewer texture sizes on the phone
	constexpr int		FIGURE_MAX_WIDTH = 480;
	constexpr int		FIGURE_MAX_HEIGHT = 560;
	constexpr int		FIGURE_PROBE = 4096;		// drawn around here first, far from any clipping, then moved into the atlas
	constexpr int		BACKDROP_WIDTH = 1400;		// the whole background picture, from the house to the street
	constexpr int		BACKDROP_HEIGHT = 600;
	constexpr uint32_t	BACKDROP_INTERVAL_MS = 3000;
	constexpr int		ATLAS_JPEG_QUALITY = 80;
	constexpr int		BACKDROP_JPEG_QUALITY = 80;

	enum class MouseAction
	{
		MOVE,
		DOWN,
		UP
	};

	struct MouseEvent
	{
		MouseAction	mAction;
		int			mX;
		int			mY;
	};

	std::atomic<bool>	gRunning{ false };
	int					gListenFd = -1;
	int					gPort = 0;
	std::string			gCode;
	std::string			gJoinText;				// game thread only
	std::string			gError;					// game thread only: why it isn't listening
	uint32_t			gLastListenTry = 0;
	std::thread			gAcceptThread;
	std::thread			gEncodeThread;
	std::atomic<int>	gViewers{ 0 };
	std::atomic<uint32_t> gLastPoll{ 0 };		// SDL ticks of the last /frame.jpg, 0 for none
	uint32_t			gLastCapture = 0;

	struct StreamConnection
	{
		std::thread							mThread;
		std::shared_ptr<std::atomic<bool>>	mDone;
	};
	std::vector<StreamConnection> gStreams;		// accept thread only

	std::mutex			gMouseMutex;
	std::deque<MouseEvent> gMouse;
	bool				gMouseHeld = false;		// game thread only
	int					gMouseX = 0;
	int					gMouseY = 0;

	// PVZ_MOD_TEST_SHOTS: a frame of the game a moment after each tap, to check what it did
	uint32_t			gShotAt = 0;
	std::string			gShotName;
	int					gShotCount = 0;

	// The latest JPEG, shared with every stream connection
	std::mutex			gFrameMutex;
	std::condition_variable gFrameCond;
	std::shared_ptr<const std::vector<unsigned char>> gFrame;
	uint64_t			gFrameNumber = 0;

	// Pixels handed from the GL thread to the encoder thread: RGBA, bottom-up
	std::mutex			gEncodeMutex;
	std::condition_variable gEncodeCond;
	std::vector<unsigned char> gRaw;
	int					gRawWidth = 0;
	int					gRawHeight = 0;
	bool				gRawReady = false;

	// The diorama. Scenes are encoded on their own thread and published under gFrameMutex, like the frames.
	struct SceneJob
	{
		std::string					mJson;
		std::vector<unsigned char>	mAtlas;			// RGBA, bottom-up
		int							mAtlasHeight = 0;
		std::vector<unsigned char>	mBackdrop;		// RGBA, bottom-up; empty when it didn't change
		uint32_t					mBackdropNumber = 0;
		uint32_t					mRenderMs = 0;
	};
	struct ScenePacket
	{
		std::string					mJson;
		std::vector<unsigned char>	mAtlas;			// JPEG
	};
	std::atomic<int>	gSceneViewers{ 0 };
	std::atomic<uint32_t> gLastScenePoll{ 0 };
	std::atomic<bool>	gSceneLive{ false };		// a lawn is in play and scenes are being sent, not the flat picture
	std::thread			gSceneThread;
	std::mutex			gSceneJobMutex;
	std::condition_variable gSceneJobCond;
	SceneJob			gSceneJob;
	bool				gSceneJobReady = false;
	std::shared_ptr<const ScenePacket> gScene;		// under gFrameMutex
	uint64_t			gSceneNumber = 0;
	std::shared_ptr<const std::vector<unsigned char>> gBackdrop;
	uint32_t			gBackdropNumber = 0;
	bool				gDrawingFigures = false;	// game thread only
	uint32_t			gLastScene = 0;				// game thread only, from here on
	uint32_t			gLastBackdrop = 0;
	uint32_t			gBackdropHash = 0;
	uint32_t			gBackdropSerial = 0;
	const Board*		gBackdropBoard = nullptr;
	int					gBackdropType = -1;
	std::unique_ptr<GLImage> gCanvas;				// a big destination, so figures drawn at the probe aren't clipped

	// Measured for the log while someone watches
	std::atomic<uint32_t> gStatScenes{ 0 };
	std::atomic<uint64_t> gStatBytes{ 0 };
	std::atomic<uint32_t> gStatRenderMs{ 0 };
	std::atomic<uint32_t> gStatEncodeMs{ 0 };
	std::atomic<uint32_t> gStatFigures{ 0 };
	uint32_t			gStatSince = 0;

	bool Polled(const std::atomic<uint32_t>& theLast, uint32_t theNow)
	{
		uint32_t aLast = theLast;	// set on the accept thread, may be a little ahead of theNow
		return aLast != 0 && static_cast<int32_t>(theNow - aLast) < static_cast<int32_t>(POLL_KEEPS_MS);
	}

	bool Polling(uint32_t theNow)
	{
		return Polled(gLastPoll, theNow);
	}

	bool SendAll(int theFd, const void* theData, size_t theSize)
	{
		const char* aData = static_cast<const char*>(theData);
		while (theSize > 0)
		{
#ifdef MSG_NOSIGNAL
			ssize_t aSent = send(theFd, aData, theSize, MSG_NOSIGNAL);
#else
			ssize_t aSent = send(theFd, aData, theSize, 0);	// SO_NOSIGPIPE is set on the socket
#endif
			if (aSent <= 0)
				return false;
			aData += aSent;
			theSize -= static_cast<size_t>(aSent);
		}
		return true;
	}

	bool SendText(int theFd, const std::string& theText)
	{
		return SendAll(theFd, theText.data(), theText.size());
	}

	void SendResponse(int theFd, const char* theStatus, const char* theType, const std::string& theBody)
	{
		SendText(theFd, std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n{}",
			theStatus, theType, theBody.size(), theBody));
	}

	std::string QueryValue(const std::string& theQuery, const std::string& theName)
	{
		size_t aPos = 0;
		while (aPos < theQuery.size())
		{
			size_t aEnd = theQuery.find('&', aPos);
			if (aEnd == std::string::npos)
				aEnd = theQuery.size();
			std::string aPair = theQuery.substr(aPos, aEnd - aPos);
			size_t aEq = aPair.find('=');
			if (aEq != std::string::npos && aPair.substr(0, aEq) == theName)
				return aPair.substr(aEq + 1);
			aPos = aEnd + 1;
		}
		return "";
	}

	std::vector<unsigned char> CompressJpeg(const std::vector<unsigned char>& theRgb, int theWidth, int theHeight, int theQuality)
	{
		jpeg_compress_struct aInfo;
		jpeg_error_mgr aError;
		aInfo.err = jpeg_std_error(&aError);
		jpeg_create_compress(&aInfo);
		unsigned char* aBuffer = nullptr;
		unsigned long aSize = 0;
		jpeg_mem_dest(&aInfo, &aBuffer, &aSize);
		aInfo.image_width = static_cast<JDIMENSION>(theWidth);
		aInfo.image_height = static_cast<JDIMENSION>(theHeight);
		aInfo.input_components = 3;
		aInfo.in_color_space = JCS_RGB;
		jpeg_set_defaults(&aInfo);
		jpeg_set_quality(&aInfo, theQuality, TRUE);
		jpeg_start_compress(&aInfo, TRUE);
		while (aInfo.next_scanline < aInfo.image_height)
		{
			JSAMPROW aRow = const_cast<unsigned char*>(theRgb.data()) + static_cast<size_t>(aInfo.next_scanline) * theWidth * 3;
			jpeg_write_scanlines(&aInfo, &aRow, 1);
		}
		jpeg_finish_compress(&aInfo);
		std::vector<unsigned char> aJpeg(aBuffer, aBuffer + aSize);
		free(aBuffer);
		jpeg_destroy_compress(&aInfo);
		return aJpeg;
	}

	// Box-filters the bottom-up RGBA capture down by a whole factor that keeps it at least MIN_FRAME_WIDTH wide
	// (the page draws it at the game's size), top-down RGB, as a JPEG.
	std::vector<unsigned char> EncodeFrame(const std::vector<unsigned char>& theRaw, int theWidth, int theHeight)
	{
		int aFactor = std::max(1, theWidth / MIN_FRAME_WIDTH);
		int aOutWidth = theWidth / aFactor;
		int aOutHeight = theHeight / aFactor;
		std::vector<unsigned char> aRgb(static_cast<size_t>(aOutWidth) * aOutHeight * 3);
		int aArea = aFactor * aFactor;
		for (int y = 0; y < aOutHeight; y++)
		{
			for (int x = 0; x < aOutWidth; x++)
			{
				int aSum[3] = { 0, 0, 0 };
				for (int dy = 0; dy < aFactor; dy++)
				{
					const unsigned char* aSrc = theRaw.data() + (static_cast<size_t>(theHeight - 1 - (y * aFactor + dy)) * theWidth + x * aFactor) * 4;
					for (int dx = 0; dx < aFactor; dx++, aSrc += 4)
					{
						aSum[0] += aSrc[0];
						aSum[1] += aSrc[1];
						aSum[2] += aSrc[2];
					}
				}
				unsigned char* aDst = aRgb.data() + (static_cast<size_t>(y) * aOutWidth + x) * 3;
				aDst[0] = static_cast<unsigned char>(aSum[0] / aArea);
				aDst[1] = static_cast<unsigned char>(aSum[1] / aArea);
				aDst[2] = static_cast<unsigned char>(aSum[2] / aArea);
			}
		}
		return CompressJpeg(aRgb, aOutWidth, aOutHeight, JPEG_QUALITY);
	}

	// The atlas, bottom-up RGBA, as a JPEG twice as wide: the (premultiplied) colors on the left, alpha on the right.
	std::vector<unsigned char> EncodeAtlas(const std::vector<unsigned char>& theRaw, int theWidth, int theHeight)
	{
		std::vector<unsigned char> aRgb(static_cast<size_t>(theWidth) * 2 * theHeight * 3);
		for (int y = 0; y < theHeight; y++)
		{
			const unsigned char* aSrc = theRaw.data() + static_cast<size_t>(theHeight - 1 - y) * theWidth * 4;
			unsigned char* aColor = aRgb.data() + static_cast<size_t>(y) * theWidth * 6;
			unsigned char* aAlpha = aColor + static_cast<size_t>(theWidth) * 3;
			for (int x = 0; x < theWidth; x++, aSrc += 4, aColor += 3, aAlpha += 3)
			{
				aColor[0] = aSrc[0];
				aColor[1] = aSrc[1];
				aColor[2] = aSrc[2];
				aAlpha[0] = aAlpha[1] = aAlpha[2] = aSrc[3];
			}
		}
		return CompressJpeg(aRgb, theWidth * 2, theHeight, ATLAS_JPEG_QUALITY);
	}

	std::vector<unsigned char> EncodeBackdrop(const std::vector<unsigned char>& theRaw, int theWidth, int theHeight)
	{
		std::vector<unsigned char> aRgb(static_cast<size_t>(theWidth) * theHeight * 3);
		for (int y = 0; y < theHeight; y++)
		{
			const unsigned char* aSrc = theRaw.data() + static_cast<size_t>(theHeight - 1 - y) * theWidth * 4;
			unsigned char* aDst = aRgb.data() + static_cast<size_t>(y) * theWidth * 3;
			for (int x = 0; x < theWidth; x++, aSrc += 4, aDst += 3)
			{
				aDst[0] = aSrc[0];
				aDst[1] = aSrc[1];
				aDst[2] = aSrc[2];
			}
		}
		return CompressJpeg(aRgb, theWidth, theHeight, BACKDROP_JPEG_QUALITY);
	}

	void EncodeLoop()
	{
		std::vector<unsigned char> aRaw;
		while (true)
		{
			int aWidth = 0, aHeight = 0;
			{
				std::unique_lock<std::mutex> aLock(gEncodeMutex);
				gEncodeCond.wait(aLock, [] { return gRawReady || !gRunning; });
				if (!gRunning)
					return;
				aRaw.swap(gRaw);
				aWidth = gRawWidth;
				aHeight = gRawHeight;
				gRawReady = false;
			}
			auto aJpeg = std::make_shared<const std::vector<unsigned char>>(EncodeFrame(aRaw, aWidth, aHeight));
			{
				std::lock_guard<std::mutex> aLock(gFrameMutex);
				gFrame = aJpeg;
				gFrameNumber++;
			}
			gFrameCond.notify_all();
		}
	}

	void SceneEncodeLoop()
	{
		SceneJob aJob;
		while (true)
		{
			{
				std::unique_lock<std::mutex> aLock(gSceneJobMutex);
				gSceneJobCond.wait(aLock, [] { return gSceneJobReady || !gRunning; });
				if (!gRunning)
					return;
				std::swap(aJob, gSceneJob);
				gSceneJobReady = false;
			}
			uint32_t aStart = SDL_GetTicks();
			std::shared_ptr<const std::vector<unsigned char>> aBackdrop;
			if (!aJob.mBackdrop.empty())
				aBackdrop = std::make_shared<const std::vector<unsigned char>>(EncodeBackdrop(aJob.mBackdrop, BACKDROP_WIDTH, BACKDROP_HEIGHT));
			auto aPacket = std::make_shared<ScenePacket>();
			aPacket->mJson = std::move(aJob.mJson);
			aPacket->mAtlas = EncodeAtlas(aJob.mAtlas, ATLAS_WIDTH, aJob.mAtlasHeight);
			gStatScenes++;
			gStatBytes += aPacket->mJson.size() + aPacket->mAtlas.size() + (aBackdrop ? aBackdrop->size() : 0);
			gStatEncodeMs += SDL_GetTicks() - aStart;
			gStatRenderMs += aJob.mRenderMs;
			{
				std::lock_guard<std::mutex> aLock(gFrameMutex);
				if (aBackdrop)
				{
					gBackdrop = aBackdrop;
					gBackdropNumber = aJob.mBackdropNumber;
				}
				gScene = aPacket;
				gSceneNumber++;
			}
			gFrameCond.notify_all();
		}
	}

	// One part of /scene: the json, then the picture (the atlas, or the screen when "flat"), then the backdrop if any.
	bool SendScenePart(int theFd, const std::string& theJson, const std::vector<unsigned char>* thePicture, const std::vector<unsigned char>* theBackdrop)
	{
		size_t aPicture = thePicture ? thePicture->size() : 0;
		size_t aBackdrop = theBackdrop ? theBackdrop->size() : 0;
		return SendText(theFd, std::format("--frame\r\nContent-Type: application/octet-stream\r\nContent-Length: {}\r\nX-Parts: {},{},{}\r\n\r\n",
				theJson.size() + aPicture + aBackdrop, theJson.size(), aPicture, aBackdrop)) &&
			SendText(theFd, theJson) &&
			(!thePicture || SendAll(theFd, thePicture->data(), aPicture)) &&
			(!theBackdrop || SendAll(theFd, theBackdrop->data(), aBackdrop)) &&
			SendText(theFd, "\r\n");
	}

	// /scene: the diorama's scenes while a lawn is in play, the flat picture otherwise. Each connection gets the
	// backdrop with its first scene and again whenever it changes.
	void SceneLoop(int theFd, std::shared_ptr<std::atomic<bool>> theDone)
	{
		gSceneViewers++;
		PvzpLogLn("ARView: diorama viewer connected ({} watching)", gSceneViewers.load());
		bool aOk = SendText(theFd, "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n"
			"Cache-Control: no-store\r\nConnection: close\r\n\r\n");
		uint64_t aLastScene = 0;
		uint64_t aLastFrame = 0;
		uint32_t aSentBackdrop = 0;
		static const std::string sFlat = "{\"flat\":1}";
		while (aOk && gRunning)
		{
			std::shared_ptr<const ScenePacket> aScene;
			std::shared_ptr<const std::vector<unsigned char>> aFrame, aBackdrop;
			{
				std::unique_lock<std::mutex> aLock(gFrameMutex);
				gFrameCond.wait_for(aLock, std::chrono::seconds(1), [&] {
					return !gRunning || (gSceneLive ? gSceneNumber != aLastScene : gFrameNumber != aLastFrame);
				});
				if (!gRunning)
					continue;
				if (gSceneLive && gScene && gSceneNumber != aLastScene)
				{
					aLastScene = gSceneNumber;
					aScene = gScene;
					if (gBackdrop && gBackdropNumber != aSentBackdrop)
					{
						aBackdrop = gBackdrop;
						aSentBackdrop = gBackdropNumber;
					}
				}
				else if (!gSceneLive && gFrame && gFrameNumber != aLastFrame)
				{
					aLastFrame = gFrameNumber;
					aFrame = gFrame;
				}
				else
					continue;
			}
			if (aScene)
				aOk = SendScenePart(theFd, aScene->mJson, &aScene->mAtlas, aBackdrop.get());
			else
				aOk = SendScenePart(theFd, sFlat, aFrame.get(), nullptr);
		}
		close(theFd);
		gSceneViewers--;
		PvzpLogLn("ARView: diorama viewer left ({} watching)", gSceneViewers.load());
		*theDone = true;
	}

	void StreamLoop(int theFd, std::shared_ptr<std::atomic<bool>> theDone)
	{
		gViewers++;
		PvzpLogLn("ARView: viewer connected ({} watching)", gViewers.load());
		bool aOk = SendText(theFd, "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n"
			"Cache-Control: no-store\r\nConnection: close\r\n\r\n");
		uint64_t aLastFrame = 0;
		while (aOk && gRunning)
		{
			std::shared_ptr<const std::vector<unsigned char>> aFrame;
			{
				std::unique_lock<std::mutex> aLock(gFrameMutex);
				gFrameCond.wait_for(aLock, std::chrono::seconds(1), [&] { return gFrameNumber != aLastFrame || !gRunning; });
				if (!gRunning || gFrameNumber == aLastFrame || !gFrame)
					continue;
				aLastFrame = gFrameNumber;
				aFrame = gFrame;
			}
			aOk = SendText(theFd, std::format("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n", aFrame->size())) &&
				SendAll(theFd, aFrame->data(), aFrame->size()) && SendText(theFd, "\r\n");
		}
		close(theFd);
		gViewers--;
		PvzpLogLn("ARView: viewer left ({} watching)", gViewers.load());
		*theDone = true;
	}

	void QueueMouse(MouseAction theAction, int theX, int theY)
	{
		std::lock_guard<std::mutex> aLock(gMouseMutex);
		if (theAction == MouseAction::MOVE && !gMouse.empty() && gMouse.back().mAction == MouseAction::MOVE)
			gMouse.back() = { theAction, theX, theY };	// only the latest position matters
		else if (gMouse.size() < MAX_QUEUED_MOUSE || theAction == MouseAction::UP)
			gMouse.push_back({ theAction, theX, theY });
	}

	std::string BuildPage()
	{
		std::string aPage = AR_PAGE;
		aPage.replace(aPage.find("{GAME_W}"), 8, std::to_string(gLawnApp->mWidth));
		aPage.replace(aPage.find("{GAME_H}"), 8, std::to_string(gLawnApp->mHeight));
		return aPage;
	}

	void HandleConnection(int theFd)
	{
		int aOne = 1;
		setsockopt(theFd, IPPROTO_TCP, TCP_NODELAY, &aOne, sizeof(aOne));
#ifdef SO_NOSIGPIPE
		setsockopt(theFd, SOL_SOCKET, SO_NOSIGPIPE, &aOne, sizeof(aOne));
#endif
		timeval aTimeout = { 2, 0 };
		setsockopt(theFd, SOL_SOCKET, SO_RCVTIMEO, &aTimeout, sizeof(aTimeout));
		setsockopt(theFd, SOL_SOCKET, SO_SNDTIMEO, &aTimeout, sizeof(aTimeout));

		std::string aRequest;
		char aBuffer[2048];
		while (aRequest.find("\r\n\r\n") == std::string::npos && aRequest.size() < 8192)
		{
			ssize_t aRead = recv(theFd, aBuffer, sizeof(aBuffer), 0);
			if (aRead <= 0)
				break;
			aRequest.append(aBuffer, static_cast<size_t>(aRead));
		}

		size_t aLineEnd = aRequest.find("\r\n");
		std::string aLine = aRequest.substr(0, aLineEnd);
		size_t aSpace1 = aLine.find(' ');
		size_t aSpace2 = aLine.find(' ', aSpace1 + 1);
		if (aSpace1 == std::string::npos || aSpace2 == std::string::npos || aLine.substr(0, aSpace1) != "GET")
		{
			SendResponse(theFd, "400 Bad Request", "text/plain", "");
			close(theFd);
			return;
		}
		std::string aTarget = aLine.substr(aSpace1 + 1, aSpace2 - aSpace1 - 1);
		size_t aQuestion = aTarget.find('?');
		std::string aPath = aTarget.substr(0, aQuestion);
		std::string aQuery = aQuestion == std::string::npos ? "" : aTarget.substr(aQuestion + 1);

		if (QueryValue(aQuery, "c") != gCode)
		{
			SendResponse(theFd, "403 Forbidden", "text/plain; charset=utf-8", "Falta el codigo: usa la direccion que sale en el juego (menu de trucos, Ver en AR).\n");
			close(theFd);
			return;
		}

		if (aPath == "/stream" || aPath == "/scene")
		{
			for (size_t i = 0; i < gStreams.size();)
			{
				if (*gStreams[i].mDone)
				{
					gStreams[i].mThread.join();
					gStreams.erase(gStreams.begin() + static_cast<long>(i));
				}
				else
					i++;
			}
			if (gStreams.size() >= MAX_VIEWERS)
			{
				SendResponse(theFd, "503 Service Unavailable", "text/plain", "");
				close(theFd);
				return;
			}
			auto aDone = std::make_shared<std::atomic<bool>>(false);
			gStreams.push_back({ std::thread(aPath == "/scene" ? SceneLoop : StreamLoop, theFd, aDone), aDone });
			return;
		}
		if (aPath == "/scene.bin" || aPath == "/scene.json" || aPath == "/atlas.jpg" || aPath == "/backdrop.jpg")
		{
			// One scene for browsers that can't read the stream (the backdrop always comes with it), and its pieces
			// for tests. 503 until the first scene of a lawn in play.
			gLastScenePoll = std::max<uint32_t>(1, SDL_GetTicks());
			std::shared_ptr<const ScenePacket> aScene;
			std::shared_ptr<const std::vector<unsigned char>> aBackdrop;
			bool aLive = gSceneLive;
			{
				std::lock_guard<std::mutex> aLock(gFrameMutex);
				aScene = gScene;
				aBackdrop = gBackdrop;
			}
			if (!aLive || !aScene || !aBackdrop)
				SendResponse(theFd, "503 Service Unavailable", "text/plain", "");
			else if (aPath == "/scene.json")
				SendResponse(theFd, "200 OK", "application/json", aScene->mJson);
			else if (aPath == "/scene.bin")
			{
				bool aOk = SendText(theFd, std::format("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: {}\r\nX-Parts: {},{},{}\r\n"
						"Cache-Control: no-store\r\nConnection: close\r\n\r\n",
						aScene->mJson.size() + aScene->mAtlas.size() + aBackdrop->size(), aScene->mJson.size(), aScene->mAtlas.size(), aBackdrop->size())) &&
					SendText(theFd, aScene->mJson) && SendAll(theFd, aScene->mAtlas.data(), aScene->mAtlas.size());
				if (aOk)
					SendAll(theFd, aBackdrop->data(), aBackdrop->size());
			}
			else
			{
				const std::vector<unsigned char>& aJpeg = aPath == "/atlas.jpg" ? aScene->mAtlas : *aBackdrop;
				if (SendText(theFd, std::format("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", aJpeg.size())))
					SendAll(theFd, aJpeg.data(), aJpeg.size());
			}
			close(theFd);
			return;
		}
		if (aPath == "/frame.jpg")
		{
			// For browsers that can't read the stream: the latest frame (the first one asked for may be old)
			gLastPoll = std::max<uint32_t>(1, SDL_GetTicks());
			std::shared_ptr<const std::vector<unsigned char>> aFrame;
			{
				std::lock_guard<std::mutex> aLock(gFrameMutex);
				aFrame = gFrame;
			}
			if (!aFrame)
				SendResponse(theFd, "503 Service Unavailable", "text/plain", "");
			else if (SendText(theFd, std::format("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", aFrame->size())))
				SendAll(theFd, aFrame->data(), aFrame->size());
			close(theFd);
			return;
		}
		if (aPath == "/click")
		{
			// x and y in game coordinates; a=down|up|move, or tap (down and up)
			std::string aAction = QueryValue(aQuery, "a");
			int aX = std::atoi(QueryValue(aQuery, "x").c_str());
			int aY = std::atoi(QueryValue(aQuery, "y").c_str());
			bool aKnown = true;
			if (aAction == "down")
				QueueMouse(MouseAction::DOWN, aX, aY);
			else if (aAction == "up")
				QueueMouse(MouseAction::UP, aX, aY);
			else if (aAction == "move")
				QueueMouse(MouseAction::MOVE, aX, aY);
			else if (aAction == "tap")
			{
				QueueMouse(MouseAction::DOWN, aX, aY);
				QueueMouse(MouseAction::UP, aX, aY);
			}
			else
				aKnown = false;
			SendResponse(theFd, aKnown ? "204 No Content" : "400 Bad Request", "text/plain", "");
			close(theFd);
			return;
		}
		if (aPath == "/ar" || aPath == "/")
		{
			SendResponse(theFd, "200 OK", "text/html; charset=utf-8", BuildPage());
			close(theFd);
			return;
		}
		SendResponse(theFd, "404 Not Found", "text/plain", "");
		close(theFd);
	}

	void AcceptLoop()
	{
		while (gRunning)
		{
			pollfd aPoll = { gListenFd, POLLIN, 0 };
			if (poll(&aPoll, 1, 200) <= 0)
				continue;
			int aFd = accept(gListenFd, nullptr, nullptr);
			if (aFd >= 0)
				HandleConnection(aFd);
		}
		for (StreamConnection& aStream : gStreams)
			aStream.mThread.join();
		gStreams.clear();
	}

	// LAN first (the phone on the same Wi-Fi), then Tailscale (100.64.0.0/10), then anything else.
	std::string PickAddress()
	{
		std::string aLan, aTailscale, aOther;
		ifaddrs* aList = nullptr;
		if (getifaddrs(&aList) != 0)
			return "127.0.0.1";
		for (ifaddrs* aIf = aList; aIf; aIf = aIf->ifa_next)
		{
			if (!aIf->ifa_addr || aIf->ifa_addr->sa_family != AF_INET)
				continue;
			uint32_t aIp = ntohl(reinterpret_cast<sockaddr_in*>(aIf->ifa_addr)->sin_addr.s_addr);
			char aText[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(aIf->ifa_addr)->sin_addr, aText, sizeof(aText));
			if ((aIp >> 24) == 127)
				continue;
			bool aPrivate = (aIp >> 24) == 10 || (aIp >> 20) == 0xAC1 || (aIp >> 16) == 0xC0A8;
			if (aPrivate && aLan.empty())
				aLan = aText;
			else if ((aIp >> 22) == (0x64400000u >> 22) && aTailscale.empty())
				aTailscale = aText;
			else if (aOther.empty())
				aOther = aText;
		}
		freeifaddrs(aList);
		return !aLan.empty() ? aLan : !aTailscale.empty() ? aTailscale : !aOther.empty() ? aOther : "127.0.0.1";
	}

	// Kept in the registry so a bookmark on the phone keeps working from one game to the next.
	std::string LoadCode()
	{
		const char* aEnv = SDL_getenv("PVZ_AR_CODE");
		if (aEnv && *aEnv)
			return aEnv;
		const char* aChars = "abcdefghjkmnpqrstuvwxyz23456789";
		std::string aCode;
		if (gLawnApp->RegistryReadString("ModARCode", &aCode) && aCode.size() == 6 &&
			std::all_of(aCode.begin(), aCode.end(), [&](char c) { return SDL_strchr(aChars, c) != nullptr; }))
			return aCode;
		std::random_device aRandom;
		aCode.clear();
		for (int i = 0; i < 6; i++)
			aCode += aChars[aRandom() % 31];
		gLawnApp->RegistryWriteString("ModARCode", aCode);
		return aCode;
	}

	void Stop()
	{
		if (!gRunning)
			return;
		gRunning = false;
		gEncodeCond.notify_all();
		gSceneJobCond.notify_all();
		gFrameCond.notify_all();
		if (gAcceptThread.joinable())
			gAcceptThread.join();
		if (gEncodeThread.joinable())
			gEncodeThread.join();
		if (gSceneThread.joinable())
			gSceneThread.join();
		if (gListenFd >= 0)
		{
			close(gListenFd);
			gListenFd = -1;
		}
		{
			std::lock_guard<std::mutex> aLock(gFrameMutex);
			gFrame.reset();
			gScene.reset();
			gBackdrop.reset();
		}
		gSceneLive = false;
		gSceneJobReady = false;
		gLastScenePoll = 0;
		gBackdropBoard = nullptr;
		if (gMouseHeld)
			QueueMouse(MouseAction::UP, gMouseX, gMouseY);	// a finger was down when the phone left
		gLastPoll = 0;
		gLastListenTry = 0;
		gJoinText.clear();
		gRawReady = false;
		PvzpLogLn("ARView: stopped");
	}

	void Start(uint32_t theNow)
	{
		gLastListenTry = std::max<uint32_t>(1, theNow);
		const char* aPortEnv = SDL_getenv("PVZ_AR_PORT");
		int aPort = aPortEnv && SDL_atoi(aPortEnv) > 0 ? SDL_atoi(aPortEnv) : DEFAULT_PORT;
		int aFd = socket(AF_INET, SOCK_STREAM, 0);
		if (aFd < 0)
			return;
		int aOne = 1;
		setsockopt(aFd, SOL_SOCKET, SO_REUSEADDR, &aOne, sizeof(aOne));
		sockaddr_in aAddr = {};
		aAddr.sin_family = AF_INET;
		aAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		aAddr.sin_port = htons(static_cast<uint16_t>(aPort));
		// One fixed port, no fallback: "adb reverse" and the phone's bookmark point at it.
		if (bind(aFd, reinterpret_cast<sockaddr*>(&aAddr), sizeof(aAddr)) != 0 || listen(aFd, 8) != 0)
		{
			close(aFd);
			if (gError.empty())
				PvzpLogLn("ARView: could not listen on port {}, trying again every {} s", aPort, RETRY_LISTEN_MS / 1000);
			gError = std::format("Ver en AR: el puerto {} esta ocupado (PVZ_AR_PORT elige otro)", aPort);
			return;
		}

		gError.clear();
		gCode = LoadCode();
		gPort = aPort;
		gListenFd = aFd;
		gJoinText = std::format("http://{}:{}/ar?c={}", PickAddress(), aPort, gCode);
		gRunning = true;
		gAcceptThread = std::thread(AcceptLoop);
		gEncodeThread = std::thread(EncodeLoop);
		gSceneThread = std::thread(SceneEncodeLoop);
		PvzpLogLn("ARView: listening, open {} (by cable: adb reverse tcp:{} tcp:{} and http://localhost:{}/ar?c={})", gJoinText, aPort, aPort, aPort, gCode);

		static bool sAtExit = false;
		if (!sAtExit)
		{
			std::atexit(Stop);
			sAtExit = true;
		}
	}

	// The game's 4:3 picture inside the drawable, as GLInterface::UpdateViewport letterboxes it.
	void Capture(uint32_t theNow)
	{
		bool aFlatWanted = gViewers > 0 || Polling(theNow) || (gSceneViewers > 0 && !gSceneLive);
		if (!aFlatWanted || theNow - gLastCapture < static_cast<uint32_t>(FRAME_INTERVAL_MS))
			return;
		{
			std::lock_guard<std::mutex> aLock(gEncodeMutex);
			if (gRawReady)
				return;	// the encoder hasn't taken the previous one yet
		}
		gLastCapture = theNow;

		int aWidth = 0, aHeight = 0;
		SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(gLawnApp->mWindow), &aWidth, &aHeight);
		int aX = 0, aY = 0, aW = aWidth, aH = aHeight;
		if (aWidth * 3 > aHeight * 4)
		{
			aW = aHeight * 4 / 3;
			aX = (aWidth - aW) / 2;
		}
		else if (aWidth * 3 < aHeight * 4)
		{
			aH = aWidth * 3 / 4;
			aY = (aHeight - aH) / 2;
		}
		if (aW <= 0 || aH <= 0)
			return;

		std::vector<unsigned char> aPixels(static_cast<size_t>(aW) * aH * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(aX, aY, aW, aH, GL_RGBA, GL_UNSIGNED_BYTE, aPixels.data());
		{
			std::lock_guard<std::mutex> aLock(gEncodeMutex);
			gRaw.swap(aPixels);
			gRawWidth = aW;
			gRawHeight = aH;
			gRawReady = true;
		}
		gEncodeCond.notify_one();
	}

	// The lawn the diorama can show: in play, no dialog over it, one of the five outdoor backgrounds, no boss.
	Board* SceneBoard()
	{
		Board* aBoard = gLawnApp->mBoard;
		if (!aBoard || gLawnApp->mGameScene != GameScenes::SCENE_PLAYING || gLawnApp->GetDialogCount() > 0)
			return nullptr;
		if (aBoard->mBackground < BackgroundType::BACKGROUND_1_DAY || aBoard->mBackground > BackgroundType::BACKGROUND_5_ROOF)
			return nullptr;
		if (gLawnApp->IsFinalBossLevel() || aBoard->mX != 0 || aBoard->mY != 0)
			return nullptr;
		return aBoard;
	}

	struct Figure
	{
		std::string	mId;
		char		mKind = ' ';
		int			mRow = 0;
		float		mFeetX = 0;		// where it stands on the ground, in game coordinates
		float		mFeetY = 0;
		Rect		mAtlas;
		int			mGameX = 0;		// the atlas rectangle's top-left on the game's screen
		int			mGameY = 0;
		int			mFacing = 1;
		int			mOrder = 0;
		int			mClickX = -1;	// where a tap on it clicks, -1 for nowhere
		int			mClickY = -1;
	};

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

		Painter(GLInterface* theGL) : mGL(theGL), mBase(gCanvas.get())
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

	std::string FigureJson(const Figure& theFigure)
	{
		return std::format("[\"{}\",\"{}\",{},{:.1f},{:.1f},{},{},{},{},{:.1f},{:.1f},{},{},{},{}]", theFigure.mId, theFigure.mKind, theFigure.mRow,
			theFigure.mFeetX, theFigure.mFeetY, theFigure.mAtlas.mX, theFigure.mAtlas.mY, theFigure.mAtlas.mWidth, theFigure.mAtlas.mHeight,
			theFigure.mFeetX - theFigure.mGameX, theFigure.mFeetY - theFigure.mGameY, theFigure.mFacing, theFigure.mOrder, theFigure.mClickX, theFigure.mClickY);
	}

	// A flat piece of the screen (the seed bank, the level's progress, a message): its atlas rectangle and where
	// it is on the game's screen.
	std::string PanelJson(const Figure& theFigure)
	{
		return std::format("[{},{},{},{},{},{}]", theFigure.mAtlas.mX, theFigure.mAtlas.mY, theFigure.mAtlas.mWidth, theFigure.mAtlas.mHeight,
			theFigure.mGameX, theFigure.mGameY);
	}

	// The background with what lies flat on it (ice, craters), from the house to the street.
	bool PaintBackdrop(GLInterface* theGL, Board* theBoard, std::vector<unsigned char>& thePixels)
	{
		if (!theGL->BeginOffscreen(BACKDROP_WIDTH, BACKDROP_HEIGHT))
			return false;
		Graphics g(gCanvas.get());
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
		theGL->EndOffscreen(&thePixels, BACKDROP_WIDTH, BACKDROP_HEIGHT);
		return true;
	}

	uint32_t SampleHash(const std::vector<unsigned char>& thePixels)
	{
		uint32_t aHash = 2166136261u;
		for (size_t i = 0; i < thePixels.size(); i += 389)
			aHash = (aHash ^ thePixels[i]) * 16777619u;
		return aHash;
	}

	// Every object of the lawn into the atlas, and the scene's json; returns the atlas height used.
	int PaintFigures(GLInterface* theGL, Board* theBoard, uint32_t theNow, std::vector<Figure>& theFigures, std::string& theJson)
	{
		Painter aPainter(theGL);
		Figure aHud, aMeter, aMessage;
		bool aHasHud = aPainter.Paint([&](Graphics* g) {
			theBoard->DrawUIBottom(g);
			if (theBoard->StageHasFog())
				theBoard->DrawTopRightUI(g);
		}, aHud, true);
		bool aHasMeter = aPainter.Paint([&](Graphics* g) {
			theBoard->DrawProgressMeter(g);
			theBoard->DrawLevel(g);
		}, aMeter, true);
		bool aHasMessage = aPainter.Paint([&](Graphics* g) { theBoard->mAdvice->Draw(g); }, aMessage, true);
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
			aFigure.mId = std::format("z{}", theBoard->mZombies.DataArrayGetID(aZombie));
			aFigure.mKind = 'z';
			aFigure.mRow = aZombie->mRow;
			aFigure.mFacing = aZombie->IsWalkingBackwards() ? 1 : -1;
			aFigure.mOrder = aZombie->mRenderOrder;
			if (aWhack)
			{
				aFigure.mClickX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2;
				aFigure.mClickY = aFigure.mGameY + aFigure.mAtlas.mHeight / 2;
			}
			theFigures.push_back(aFigure);
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
				aFigure.mId = std::format("p{}", aCol * MAX_GRID_SIZE_Y + aRow);
				aFigure.mKind = 'p';
				aFigure.mRow = aRow;
				aFigure.mOrder = aMain->mRenderOrder;
				aFigure.mClickX = static_cast<int>(aFigure.mFeetX);
				aFigure.mClickY = aCellY + 40;
				theFigures.push_back(aFigure);
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
			aFigure.mId = std::format("g{}", theBoard->mGridItems.DataArrayGetID(aGridItem));
			aFigure.mKind = 'g';
			aFigure.mRow = aGridItem->mGridY;
			aFigure.mOrder = aGridItem->mRenderOrder;
			if (aGridItem->mGridX >= 0 && aGridItem->mGridX < MAX_GRID_SIZE_X && aGridItem->mGridY >= 0 && aGridItem->mGridY < MAX_GRID_SIZE_Y)
			{
				aFigure.mClickX = theBoard->GridToPixelX(aGridItem->mGridX, aGridItem->mGridY) + 40;
				aFigure.mClickY = theBoard->GridToPixelY(aGridItem->mGridX, aGridItem->mGridY) + 40;
			}
			theFigures.push_back(aFigure);
		}

		for (LawnMower* aLawnMower : theBoard->mLawnMowers)
		{
			if (aLawnMower->mDead)
				continue;
			Figure aFigure;
			if (!aPainter.Paint([&](Graphics* g) { aLawnMower->Draw(g); }, aFigure))
				continue;
			aFigure.mFeetX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2.0f;
			aFigure.mFeetY = static_cast<float>(aFigure.mGameY + aFigure.mAtlas.mHeight);
			aFigure.mId = std::format("m{}", theBoard->mLawnMowers.DataArrayGetID(aLawnMower));
			aFigure.mKind = 'm';
			aFigure.mRow = aLawnMower->mRow;
			aFigure.mOrder = aLawnMower->mRenderOrder;
			theFigures.push_back(aFigure);
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
			aFigure.mId = std::format("c{}", theBoard->mCoins.DataArrayGetID(aCoin));
			aFigure.mKind = 'c';
			aFigure.mRow = aCoin->mRow;
			aFigure.mOrder = aCoin->mRenderOrder;
			if (!aCoin->mIsBeingCollected)
			{
				aFigure.mClickX = aFigure.mGameX + aFigure.mAtlas.mWidth / 2;
				aFigure.mClickY = aFigure.mGameY + aFigure.mAtlas.mHeight / 2;
			}
			theFigures.push_back(aFigure);
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
			aFigure.mId = std::format("b{}", theBoard->mProjectiles.DataArrayGetID(aProjectile));
			aFigure.mKind = 'b';
			aFigure.mRow = aProjectile->mRow;
			aFigure.mFacing = aProjectile->mVelX < 0 ? -1 : 1;
			aFigure.mOrder = aProjectile->mRenderOrder;
			theFigures.push_back(aFigure);
		}

		std::string aObjects;
		for (const Figure& aFigure : theFigures)
		{
			if (!aObjects.empty())
				aObjects += ',';
			aObjects += FigureJson(aFigure);
		}
		std::string aLanes;
		std::string aWater;
		int aRows = theBoard->StageHasPool() ? 6 : 5;
		for (int aRow = 0; aRow < aRows; aRow++)
		{
			aLanes += std::format("{}{}", aRow ? "," : "", theBoard->GridToPixelY(8, aRow));
			aWater += std::format("{}{}", aRow ? "," : "", theBoard->mPlantRow[aRow] == PlantRowType::PLANTROW_POOL ? 1 : 0);
		}
		if (aPainter.mSkipped > 0)
			PvzpLogLn("ARView: the atlas is full, {} figures left out", aPainter.mSkipped);
		theJson = std::format("{{\"t\":{},\"aw\":{},\"ah\":{},\"bg\":{},\"bgx\":{},\"bgw\":{},\"bgh\":{},\"bt\":{},\"lanes\":[{}],\"water\":[{}],"
			"\"hud\":{},\"meter\":{},\"msg\":{},\"o\":[{}]}}",
			theNow, ATLAS_WIDTH, aPainter.UsedHeight(), gBackdropSerial, -BOARD_OFFSET, BACKDROP_WIDTH, BACKDROP_HEIGHT, static_cast<int>(theBoard->mBackground),
			aLanes, aWater, aHasHud ? PanelJson(aHud) : "null", aHasMeter ? PanelJson(aMeter) : "null", aHasMessage ? PanelJson(aMessage) : "null", aObjects);
		return aPainter.UsedHeight();
	}

	void Scene(uint32_t theNow)
	{
		if (gSceneViewers == 0 && !Polled(gLastScenePoll, theNow))
		{
			gSceneLive = false;
			return;
		}
		Board* aBoard = SceneBoard();
		if (!aBoard)
		{
			if (gSceneLive)
			{
				gSceneLive = false;
				gFrameCond.notify_all();
			}
			return;
		}
		if (theNow - gLastScene < SCENE_INTERVAL_MS)
			return;
		{
			std::lock_guard<std::mutex> aLock(gSceneJobMutex);
			if (gSceneJobReady)
				return;	// the encoder hasn't taken the previous one yet
		}
		gLastScene = theNow;
		GLInterface* aGL = gLawnApp->mGLInterface.get();
		if (!gCanvas)
		{
			gCanvas = std::make_unique<GLImage>(aGL);
			gCanvas->mWidth = FIGURE_PROBE * 2;
			gCanvas->mHeight = FIGURE_PROBE * 2;
			gCanvas->SetImageMode(false, false);
		}

		SceneJob aJob;
		bool aNewBoard = aBoard != gBackdropBoard || static_cast<int>(aBoard->mBackground) != gBackdropType;
		if (aNewBoard || theNow - gLastBackdrop >= BACKDROP_INTERVAL_MS)
		{
			gLastBackdrop = theNow;
			if (PaintBackdrop(aGL, aBoard, aJob.mBackdrop))
			{
				uint32_t aHash = SampleHash(aJob.mBackdrop);
				if (!aNewBoard && aHash == gBackdropHash)
					aJob.mBackdrop.clear();
				else
				{
					gBackdropHash = aHash;
					gBackdropBoard = aBoard;
					gBackdropType = static_cast<int>(aBoard->mBackground);
					aJob.mBackdropNumber = ++gBackdropSerial;
				}
			}
		}
		if (gBackdropSerial == 0)
			return;

		if (!aGL->BeginOffscreen(ATLAS_WIDTH, ATLAS_MAX_HEIGHT))
			return;
		std::vector<Figure> aFigures;
		gDrawingFigures = true;
		aJob.mAtlasHeight = PaintFigures(aGL, aBoard, theNow, aFigures, aJob.mJson);
		gDrawingFigures = false;
		aGL->EndOffscreen(&aJob.mAtlas, ATLAS_WIDTH, aJob.mAtlasHeight);
		aJob.mRenderMs = SDL_GetTicks() - theNow;
		gStatFigures = static_cast<uint32_t>(aFigures.size());
		{
			std::lock_guard<std::mutex> aLock(gSceneJobMutex);
			std::swap(gSceneJob, aJob);
			gSceneJobReady = true;
		}
		gSceneJobCond.notify_one();
		gSceneLive = true;

		if (gStatSince == 0)
			gStatSince = theNow;
		else if (theNow - gStatSince >= 10000)
		{
			uint32_t aScenes = gStatScenes.exchange(0);
			uint64_t aBytes = gStatBytes.exchange(0);
			uint32_t aRender = gStatRenderMs.exchange(0), aEncode = gStatEncodeMs.exchange(0);
			if (aScenes > 0)
				PvzpLogLn("ARView: diorama {:.1f} scenes/s, {} KB each ({} KB/s), {} figures, draw {:.1f} ms, encode {:.1f} ms",
					aScenes * 1000.0 / (theNow - gStatSince), aBytes / aScenes / 1024, aBytes * 1000 / (theNow - gStatSince) / 1024,
					gStatFigures.load(), static_cast<double>(aRender) / aScenes, static_cast<double>(aEncode) / aScenes);
			gStatSince = theNow;
		}
	}
}

void ARView::OnFrame()
{
	static const bool sForced = [] {
		const char* aEnv = SDL_getenv("PVZ_AR_VIEW");
		return aEnv && *aEnv && SDL_strcmp(aEnv, "0") != 0;
	}();
	uint32_t aNow = SDL_GetTicks();
	bool aWanted = gLawnApp && gLawnApp->mWindow && (sForced || Mods::Settings().mARView);
	if (aWanted && !gRunning && (gLastListenTry == 0 || aNow - gLastListenTry >= RETRY_LISTEN_MS))
		Start(aNow);
	else if (!aWanted)
	{
		Stop();
		gError.clear();
	}

	if (gRunning)
	{
		Capture(aNow);
		Scene(aNow);
	}

	if (gShotAt != 0 && static_cast<int32_t>(aNow - gShotAt) >= 0)
	{
		HackMenu::RequestScreenshot(std::format("{}/{}.png", SDL_getenv("PVZ_MOD_TEST_SHOTS"), gShotName).c_str());
		gShotAt = 0;
	}
}

bool ARView::DispatchMouse()
{
	MouseEvent aEvent;
	{
		std::lock_guard<std::mutex> aLock(gMouseMutex);
		if (gMouse.empty())
			return false;
		aEvent = gMouse.front();
		gMouse.pop_front();
	}
	if (!gLawnApp || !gLawnApp->mWidgetManager)
		return true;

	// What SexyAppBase does with an SDL mouse event, minus RemapMouse: these are game coordinates already.
	WidgetManager* aManager = gLawnApp->mWidgetManager.get();
	int aX = std::clamp(aEvent.mX, 0, gLawnApp->mWidth - 1);
	int aY = std::clamp(aEvent.mY, 0, gLawnApp->mHeight - 1);
	gLawnApp->mMouseIn = true;
	gLawnApp->mLastUserInputTick = gLawnApp->mLastTimerTime;
	gMouseX = aX;
	gMouseY = aY;
	// Buttons only act on a release while the window has focus (ButtonWidget::MouseUp), and the player is
	// on the phone, so the Mac window often hasn't: a tap counts as focused.
	bool aHadFocus = aManager->mHasFocus;
	aManager->mHasFocus = true;
	aManager->MouseMove(aX, aY);
	if (aEvent.mAction == MouseAction::DOWN)
	{
		if (gMouseHeld)
			aManager->MouseUp(aX, aY, 1);	// the up got lost
		aManager->MouseDown(aX, aY, 1);
		gMouseHeld = true;

		static const char* sShots = SDL_getenv("PVZ_MOD_TEST_SHOTS");
		if (sShots)
		{
			gShotName = std::format("ar-tap{:02}-{}x{}", ++gShotCount, aX, aY);
			gShotAt = std::max<uint32_t>(1, SDL_GetTicks() + SHOT_DELAY_MS);
			PvzpLogLn("ARView: tap {} at {},{} (window focused: {})", gShotCount, aX, aY, aHadFocus);
		}
	}
	else if (aEvent.mAction == MouseAction::UP && gMouseHeld)
	{
		aManager->MouseUp(aX, aY, 1);
		gMouseHeld = false;
	}
	aManager->mHasFocus = aHadFocus;
	return true;
}

std::string ARView::HintText()
{
	if (!gRunning)
		return gError;
	return std::format("Ver en AR: abre {} en el celular\nCon cable: adb reverse tcp:{} tcp:{} y http://localhost:{}/ar?c={}",
		gJoinText, gPort, gPort, gPort, gCode);
}

bool ARView::HasViewer()
{
	uint32_t aNow = SDL_GetTicks();
	return gViewers > 0 || gSceneViewers > 0 || Polling(aNow) || Polled(gLastScenePoll, aNow);
}

bool ARView::DrawingFigures()
{
	return gDrawingFigures;
}

#endif
