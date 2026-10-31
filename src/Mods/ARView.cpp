#include "ARView.h"
#include "Mods.h"
#include "HackMenu.h"
#include "LawnApp.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
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
	constexpr int		MAX_VIEWERS = 4;
	constexpr size_t	MAX_QUEUED_MOUSE = 64;
	constexpr uint32_t	POLL_KEEPS_MS = 2000;		// a /frame.jpg keeps the capture going this long
	constexpr uint32_t	RETRY_LISTEN_MS = 5000;
	constexpr uint32_t	SHOT_DELAY_MS = 700;

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

	bool Polling(uint32_t theNow)
	{
		uint32_t aLast = gLastPoll;	// set on the accept thread, may be a little ahead of theNow
		return aLast != 0 && static_cast<int32_t>(theNow - aLast) < static_cast<int32_t>(POLL_KEEPS_MS);
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

		jpeg_compress_struct aInfo;
		jpeg_error_mgr aError;
		aInfo.err = jpeg_std_error(&aError);
		jpeg_create_compress(&aInfo);
		unsigned char* aBuffer = nullptr;
		unsigned long aSize = 0;
		jpeg_mem_dest(&aInfo, &aBuffer, &aSize);
		aInfo.image_width = static_cast<JDIMENSION>(aOutWidth);
		aInfo.image_height = static_cast<JDIMENSION>(aOutHeight);
		aInfo.input_components = 3;
		aInfo.in_color_space = JCS_RGB;
		jpeg_set_defaults(&aInfo);
		jpeg_set_quality(&aInfo, JPEG_QUALITY, TRUE);
		jpeg_start_compress(&aInfo, TRUE);
		while (aInfo.next_scanline < aInfo.image_height)
		{
			JSAMPROW aRow = aRgb.data() + static_cast<size_t>(aInfo.next_scanline) * aOutWidth * 3;
			jpeg_write_scanlines(&aInfo, &aRow, 1);
		}
		jpeg_finish_compress(&aInfo);
		std::vector<unsigned char> aJpeg(aBuffer, aBuffer + aSize);
		free(aBuffer);
		jpeg_destroy_compress(&aInfo);
		return aJpeg;
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

		if (aPath == "/stream")
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
			gStreams.push_back({ std::thread(StreamLoop, theFd, aDone), aDone });
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
		gFrameCond.notify_all();
		if (gAcceptThread.joinable())
			gAcceptThread.join();
		if (gEncodeThread.joinable())
			gEncodeThread.join();
		if (gListenFd >= 0)
		{
			close(gListenFd);
			gListenFd = -1;
		}
		{
			std::lock_guard<std::mutex> aLock(gFrameMutex);
			gFrame.reset();
		}
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
		if ((gViewers == 0 && !Polling(theNow)) || theNow - gLastCapture < static_cast<uint32_t>(FRAME_INTERVAL_MS))
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
		Capture(aNow);

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
	return gViewers > 0 || Polling(SDL_GetTicks());
}

#endif
