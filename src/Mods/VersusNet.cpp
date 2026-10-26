#include "VersusNet.h"
#include "Versus.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "PvzpLib/PvzpDebug.h"
#include "SexyAppFramework/graphics/GLPlatform.h"
#include <SDL.h>

#ifdef _WIN32

void VersusNet::OnFrame()
{
}

#else

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
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
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace
{
	constexpr int		DEFAULT_PORT = 8090;
	constexpr int		FRAME_INTERVAL_MS = 80;		// about 12 frames a second for the viewer
	constexpr int		MAX_FRAME_WIDTH = 960;		// bigger drawables are scaled down by a whole factor
	constexpr int		JPEG_QUALITY = 70;
	constexpr int		MAX_VIEWERS = 4;
	constexpr int		MAX_QUEUED_KEYS = 32;
	constexpr uint32_t	STOP_AFTER_MS = 5000;		// keeps listening this long after the versus board is gone

	std::atomic<bool>	gRunning{ false };
	int					gListenFd = -1;
	std::string			gCode;
	std::string			gJoinText;					// game thread only
	std::thread			gFunnelThread;
	std::mutex			gFunnelMutex;
	std::string			gPublicUrl;					// the Tailscale Funnel address, once it is up
	std::atomic<bool>	gFunnelOn{ false };
	std::thread			gAcceptThread;
	std::thread			gEncodeThread;
	std::atomic<int>	gViewers{ 0 };
	uint32_t			gLastCapture = 0;
	uint32_t			gNoBoardSince = 0;

	struct StreamConnection
	{
		std::thread							mThread;
		std::shared_ptr<std::atomic<bool>>	mDone;
	};
	std::vector<StreamConnection> gStreams;		// accept thread only

	std::mutex			gKeyMutex;
	std::deque<int>		gKeys;

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

	bool IsZombieKey(int theKey)
	{
		return theKey == 38 || theKey == 40 || theKey == 'W' || theKey == 'S' || (theKey >= '1' && theKey <= '9');
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

	// Box-filters the bottom-up RGBA capture down to at most MAX_FRAME_WIDTH wide, top-down RGB, as a JPEG.
	std::vector<unsigned char> EncodeFrame(const std::vector<unsigned char>& theRaw, int theWidth, int theHeight)
	{
		int aFactor = std::max(1, (theWidth + MAX_FRAME_WIDTH - 1) / MAX_FRAME_WIDTH);
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
		PvzpLogLn("VersusNet: viewer connected ({} watching)", gViewers.load());
		bool aOk = SendText(theFd, "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n"
			"Cache-Control: no-store\r\nConnection: close\r\n\r\n");
		uint64_t aLastFrame = 0;
		while (aOk && gRunning)
		{
			std::shared_ptr<const std::vector<unsigned char>> aFrame;
			{
				std::unique_lock<std::mutex> aLock(gFrameMutex);
				gFrameCond.wait_for(aLock, std::chrono::seconds(1), [&] { return gFrameNumber != aLastFrame || !gRunning; });
				if (!gRunning || gFrameNumber == aLastFrame)
					continue;
				aLastFrame = gFrameNumber;
				aFrame = gFrame;
			}
			aOk = SendText(theFd, std::format("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: {}\r\n\r\n", aFrame->size())) &&
				SendAll(theFd, aFrame->data(), aFrame->size()) && SendText(theFd, "\r\n");
		}
		close(theFd);
		gViewers--;
		PvzpLogLn("VersusNet: viewer left ({} watching)", gViewers.load());
		*theDone = true;
	}

	std::string BuildPage()
	{
		std::string aButtons;
		for (int i = 0; i < ModVersus::NUM_ROSTER; i++)
		{
			const ModVersus::RosterEntry& aEntry = ModVersus::GetRosterEntry(i);
			aButtons += std::format("<button data-k=\"{}\"><b>{}</b> {}<small>{} cerebros</small></button>", '1' + i, i + 1, aEntry.mName, aEntry.mCost);
		}
		std::string aPage = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PvZ Versus: zombis</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#141210;color:#eee;font:15px -apple-system,system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;gap:10px;padding:10px 16px 16px}
img{width:min(100%,calc((100vh - 190px)*4/3));aspect-ratio:4/3;background:#000;border-radius:8px}
.row{display:flex;flex-wrap:wrap;gap:6px;justify-content:center;max-width:900px}
button{background:#2a2622;color:#eee;border:1px solid #4a433c;border-radius:8px;padding:10px 10px;font:inherit;cursor:pointer;display:flex;flex-direction:column;align-items:center;min-width:84px;touch-action:manipulation;user-select:none;-webkit-user-select:none}
button:active{background:#5a2a3a}
button small{color:#f39ad0;font-size:12px}
.lane button{min-width:130px;flex-direction:row;gap:6px;justify-content:center}
p{margin:0;color:#aaa;font-size:13px;text-align:center}
</style></head><body>
<img id="game" alt="El juego en vivo">
<div class="row lane"><button data-k="38">&#9650; Carril arriba</button><button data-k="40">&#9660; Carril abajo</button></div>
<div class="row">{BUTTONS}</div>
<p>Teclado: W/S o las flechas cambian el carril, 1-9 mandan el zombi. Los cerebros y el reloj se ven en la barra del juego.</p>
<script>
const code=new URLSearchParams(location.search).get('c')||'';
const img=document.getElementById('game');
function connect(){img.src='/stream?c='+code+'&t='+Date.now();}
img.onerror=()=>setTimeout(connect,1000);
connect();
function send(k){fetch('/cmd?c='+code+'&k='+k,{cache:'no-store'}).catch(()=>{});}
document.querySelectorAll('button[data-k]').forEach(b=>b.addEventListener('click',()=>send(b.dataset.k)));
const keys={ArrowUp:38,ArrowDown:40,w:87,W:87,s:83,S:83};
addEventListener('keydown',e=>{let k=keys[e.key];if(!k&&e.key>='1'&&e.key<='9')k=e.key.charCodeAt(0);if(!k)return;e.preventDefault();if(!e.repeat)send(k);});
</script></body></html>
)HTML";
		aPage.replace(aPage.find("{BUTTONS}"), 9, aButtons);
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
			SendResponse(theFd, "403 Forbidden", "text/plain; charset=utf-8", "Falta el codigo de la partida: usa la direccion que sale en la barra del juego.\n");
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
		if (aPath == "/cmd")
		{
			int aKey = std::atoi(QueryValue(aQuery, "k").c_str());
			if (IsZombieKey(aKey))
			{
				std::lock_guard<std::mutex> aLock(gKeyMutex);
				if (gKeys.size() < MAX_QUEUED_KEYS)
					gKeys.push_back(aKey);
			}
			SendResponse(theFd, "204 No Content", "text/plain", "");
			close(theFd);
			return;
		}
		if (aPath == "/")
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

	// LAN first (what two Macs on the same Wi-Fi use), then Tailscale (100.64.0.0/10), then anything else.
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
			PvzpLogLn("VersusNet: address {} on {}", aText, aIf->ifa_name);
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

	// Runs a program (no shell) and collects its output, killing it after theTimeoutMs.
	bool RunCommand(const std::vector<std::string>& theArgs, int theTimeoutMs, std::string* theOutput)
	{
		int aPipe[2];
		if (pipe(aPipe) != 0)
			return false;
		posix_spawn_file_actions_t aActions;
		posix_spawn_file_actions_init(&aActions);
		posix_spawn_file_actions_adddup2(&aActions, aPipe[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&aActions, aPipe[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&aActions, aPipe[0]);
		std::vector<char*> aArgv;
		for (const std::string& aArg : theArgs)
			aArgv.push_back(const_cast<char*>(aArg.c_str()));
		aArgv.push_back(nullptr);
		pid_t aPid = 0;
		int aSpawn = posix_spawnp(&aPid, aArgv[0], &aActions, nullptr, aArgv.data(), environ);
		posix_spawn_file_actions_destroy(&aActions);
		close(aPipe[1]);
		if (aSpawn != 0)
		{
			close(aPipe[0]);
			return false;
		}

		uint32_t aStart = SDL_GetTicks();
		char aBuffer[1024];
		while (true)
		{
			int aLeft = theTimeoutMs - static_cast<int>(SDL_GetTicks() - aStart);
			pollfd aPoll = { aPipe[0], POLLIN, 0 };
			if (aLeft <= 0 || poll(&aPoll, 1, aLeft) <= 0)
			{
				kill(aPid, SIGTERM);
				break;
			}
			ssize_t aRead = read(aPipe[0], aBuffer, sizeof(aBuffer));
			if (aRead <= 0)
				break;
			if (theOutput)
				theOutput->append(aBuffer, static_cast<size_t>(aRead));
		}
		close(aPipe[0]);
		int aStatus = 0;
		waitpid(aPid, &aStatus, 0);
		return WIFEXITED(aStatus) && WEXITSTATUS(aStatus) == 0;
	}

	std::string TailscaleCli()
	{
		const char* aApp = "/Applications/Tailscale.app/Contents/MacOS/Tailscale";
		return access(aApp, X_OK) == 0 ? aApp : "tailscale";
	}

	// Publishes the page on the internet with Tailscale Funnel (https://<this-machine>.<tailnet>.ts.net), so
	// the other player can join from any network. PVZ_VERSUS_FUNNEL=0 keeps it on the local network.
	void FunnelLoop(int thePort, std::string theCode)
	{
		const char* aEnv = SDL_getenv("PVZ_VERSUS_FUNNEL");
		if (aEnv && SDL_strcmp(aEnv, "0") == 0)
			return;

		std::string aOutput;
		RunCommand({ TailscaleCli(), "funnel", "--bg", "--yes", std::to_string(thePort) }, 15000, &aOutput);
		std::string aStatus;
		RunCommand({ TailscaleCli(), "funnel", "status" }, 5000, &aStatus);
		if (aStatus.find(std::format("127.0.0.1:{}", thePort)) == std::string::npos)
		{
			PvzpLogLn("VersusNet: no Tailscale Funnel ({}), local network only", aOutput.substr(0, aOutput.find('\n')));
			return;
		}
		gFunnelOn = true;

		std::string aJson;
		RunCommand({ TailscaleCli(), "status", "--json" }, 5000, &aJson);
		size_t aSelf = aJson.find("\"Self\"");
		size_t aKey = aSelf == std::string::npos ? std::string::npos : aJson.find("\"DNSName\": \"", aSelf);
		if (aKey == std::string::npos)
			return;
		size_t aStart = aKey + 12;
		std::string aHost = aJson.substr(aStart, aJson.find('"', aStart) - aStart);
		if (!aHost.empty() && aHost.back() == '.')
			aHost.pop_back();
		std::lock_guard<std::mutex> aLock(gFunnelMutex);
		gPublicUrl = std::format("https://{}/?c={}", aHost, theCode);
		PvzpLogLn("VersusNet: public at {}", gPublicUrl);
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
		if (gFunnelThread.joinable())
			gFunnelThread.join();
		if (gFunnelOn)
		{
			RunCommand({ TailscaleCli(), "funnel", "--https=443", "off" }, 5000, nullptr);
			gFunnelOn = false;
		}
		{
			std::lock_guard<std::mutex> aLock(gFunnelMutex);
			gPublicUrl.clear();
		}
		if (gListenFd >= 0)
		{
			close(gListenFd);
			gListenFd = -1;
		}
		{
			std::lock_guard<std::mutex> aLock(gKeyMutex);
			gKeys.clear();
		}
		gJoinText.clear();
		gRawReady = false;
		PvzpLogLn("VersusNet: stopped");
	}

	void Start()
	{
		const char* aPortEnv = SDL_getenv("PVZ_VERSUS_PORT");
		int aBasePort = aPortEnv && SDL_atoi(aPortEnv) > 0 ? SDL_atoi(aPortEnv) : DEFAULT_PORT;
		int aFd = socket(AF_INET, SOCK_STREAM, 0);
		if (aFd < 0)
			return;
		int aOne = 1;
		setsockopt(aFd, SOL_SOCKET, SO_REUSEADDR, &aOne, sizeof(aOne));
		int aPort = 0;
		for (int aTry = aBasePort; aTry < aBasePort + 10; aTry++)
		{
			sockaddr_in aAddr = {};
			aAddr.sin_family = AF_INET;
			aAddr.sin_addr.s_addr = htonl(INADDR_ANY);
			aAddr.sin_port = htons(static_cast<uint16_t>(aTry));
			if (bind(aFd, reinterpret_cast<sockaddr*>(&aAddr), sizeof(aAddr)) == 0)
			{
				aPort = aTry;
				break;
			}
		}
		if (aPort == 0 || listen(aFd, 8) != 0)
		{
			PvzpLogLn("VersusNet: could not listen on ports {}-{}", aBasePort, aBasePort + 9);
			close(aFd);
			return;
		}

		const char* aCodeEnv = SDL_getenv("PVZ_VERSUS_CODE");
		if (aCodeEnv && *aCodeEnv)
			gCode = aCodeEnv;
		else
		{
			// Six letters and digits: with the public link anyone could try codes.
			const char* aChars = "abcdefghjkmnpqrstuvwxyz23456789";
			std::random_device aRandom;
			gCode.clear();
			for (int i = 0; i < 6; i++)
				gCode += aChars[aRandom() % 31];
		}
		gListenFd = aFd;
		gJoinText = std::format("http://{}:{}/?c={}", PickAddress(), aPort, gCode);
		gRunning = true;
		gAcceptThread = std::thread(AcceptLoop);
		gEncodeThread = std::thread(EncodeLoop);
		gFunnelThread = std::thread(FunnelLoop, aPort, gCode);
		PvzpLogLn("VersusNet: listening, join at {}", gJoinText);

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
		if (gViewers == 0 || theNow - gLastCapture < static_cast<uint32_t>(FRAME_INTERVAL_MS))
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

	bool PopKey(int* theKey)
	{
		std::lock_guard<std::mutex> aLock(gKeyMutex);
		if (gKeys.empty())
			return false;
		*theKey = gKeys.front();
		gKeys.pop_front();
		return true;
	}

	std::string JoinText()
	{
		std::lock_guard<std::mutex> aLock(gFunnelMutex);
		return gPublicUrl.empty() ? gJoinText : gPublicUrl;
	}

	bool HasViewer()
	{
		return gViewers > 0;
	}
}

void VersusNet::OnFrame()
{
	static bool sHooked = false;
	if (!sHooked)
	{
		ModVersus::NetHooks aHooks;
		aHooks.mPopKey = PopKey;
		aHooks.mJoinText = JoinText;
		aHooks.mHasViewer = HasViewer;
		ModVersus::SetNetHooks(aHooks);
		sHooked = true;
	}

	uint32_t aNow = SDL_GetTicks();
	bool aVersusBoard = gLawnApp && gLawnApp->mBoard && ModVersus::IsVersus(gLawnApp->mGameMode);
	if (aVersusBoard)
	{
		gNoBoardSince = 0;
		if (!gRunning)
			Start();
	}
	else if (gRunning)
	{
		if (gNoBoardSince == 0)
			gNoBoardSince = aNow;
		else if (aNow - gNoBoardSince > STOP_AFTER_MS)
			Stop();
	}

	if (gRunning)
		Capture(aNow);
}

#endif
